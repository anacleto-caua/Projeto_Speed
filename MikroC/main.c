/* *********************************************************
Cristal 20 MHz (5 MHz)
Ciclo de máquina 200nS
Base de tempo de 1 ms -> Contador do timer0 (16 bits -  0 a 65536) inicia em    60536
     TMR0H = 0xEC;
     TMR0L = 0x78; (0X89 empiricamente)
***********************************************************/

// Teste de calibração do Timer0: alterna RB5 a cada tick do Timer0, gerando
// uma onda quadrada de referência para medir com osciloscópio/analisador
// lógico o período real do Timer0 contra o nominal de 1 ms e ajustar
// TIMER0_LOAD_HIGH/TIMER0_LOAD_LOW. RB5 não é usado por mais nada no
// firmware, então é seguro reaproveitá-lo aqui.
// Comente/descomente a linha abaixo para tirar/pôr o teste na compilação sem
// apagar o código. Desligado por padrão após a calibração de
// TIMER0_LOAD_LOW=0x9D (2026-09-17).
// #define TIMER0_CALIBRATION_TEST

// Definição de tipo
typedef unsigned char u8;
typedef signed char   i8;

typedef unsigned int  u16;
typedef signed int    i16;

typedef unsigned long u32;
typedef signed long   i32;

// String view
typedef struct {
    const char* Data; // Ponteiro para a string (armazenada em ROM)
    u8 Length;        // Tamanho da string calculado no tempo de compilação
} StrView;

#define MAKE_VIEW(str) { str, sizeof(str) - 1 }

// Mapeamento do LCD
#define LCD_COLLUMN_COUNT 20
#define LCD_LINE_COUNT    4

sbit LCD_RS at RD4_bit;
sbit LCD_EN at RD5_bit;
sbit LCD_D4 at RD0_bit;
sbit LCD_D5 at RD1_bit;
sbit LCD_D6 at RD2_bit;
sbit LCD_D7 at RD3_bit;

sbit LCD_RS_Direction at TRISD4_bit;
sbit LCD_EN_Direction at TRISD5_bit;
sbit LCD_D4_Direction at TRISD0_bit;
sbit LCD_D5_Direction at TRISD1_bit;
sbit LCD_D6_Direction at TRISD2_bit;
sbit LCD_D7_Direction at TRISD3_bit;

// Inicia contagem em 60536 - base de tempo de 1 ms
// TIMER0_LOAD_LOW ajustado para 0x9D por calibração com osciloscópio via
// TIMER0_CALIBRATION_TEST.
#define TIMER0_LOAD_HIGH 0xEC
#define TIMER0_LOAD_LOW  0x9D

// Portas do enconder
#define ENCODER_SIGNAL_PORT PORTB.B3

// Porta do dmux do led (Usa LATC para evitar problemas de Read-Modify-Write)
// Ocupa apenas os 5 bits menos significativos (RC0-RC4, índice 0 a 31);
// os bits restantes de LATC (ex.: RC5, controle do Bluetooth) são preservados.
#define LED_DMUX_PORT LATC
#define LED_DMUX_MASK  0x1F
#define SET_LED_DMUX(val) (LED_DMUX_PORT = (LED_DMUX_PORT & ~LED_DMUX_MASK) | ((val) & LED_DMUX_MASK))

// Porta de controle de alimentação do módulo Bluetooth HC-05 (RC5), usada para
// economizar bateria quando o Bluetooth não é necessário. #define para poder
// remapear o pino facilmente no futuro.
#define BLUETOOTH_CONTROL_PORT LATC.B5
#define BLUETOOTH_ON_LEVEL  0  // Nível baixo = Bluetooth ligado
#define BLUETOOTH_OFF_LEVEL 1  // Nível alto  = Bluetooth desligado

// Possiveis estados do programa
#define STATE_IDLE                       0
#define STATE_INIT_RENDER_MENU           1
#define STATE_SELECTING_MENU             2
// Assistente de configuração da bateria de testes aleatórios ("Aleatorio" no
// menu): cada passo tem um estado "INIT_" que só limpa o LCD (os rótulos de
// cada passo têm tamanhos diferentes, então reaproveitar a tela sem limpar
// deixaria pedaços do texto anterior visíveis) e um estado companheiro que
// de fato lê o encoder e redesenha a cada volta do loop principal.
#define STATE_INIT_RANDOM_CONFIG_COUNT   3
#define STATE_RANDOM_CONFIG_COUNT        4
#define STATE_INIT_RANDOM_CONFIG_REPEATS 5
#define STATE_RANDOM_CONFIG_REPEATS      6
#define STATE_INIT_RANDOM_CONFIG_VALUE   7
#define STATE_RANDOM_CONFIG_VALUE        8
// Estados "INIT_" sem tela própria: só fazem o trabalho pesado (gravar
// EEPROM, sortear a ordem dos testes) fora da ISR antes de cair em
// STATE_STARTING_TEST — ver comentário de BatteryActive.
#define STATE_INIT_RANDOM_BATTERY        9
#define STATE_INIT_RANDOM_BATTERY_REPEAT 10
#define STATE_RANDOM_BATTERY_RESULT      11
#define STATE_STARTING_TEST              12
#define STATE_TEST_READY                 13
#define STATE_TEST_BEGIN                 14
#define STATE_RUNNING_TEST               15
#define STATE_CALCULATE_TEST_RESULT      16
#define STATE_FINISHED_TEST              17
#define STATE_ERROR                      18

volatile u8 ProgramState = STATE_IDLE;

// Limites atualizados para bater com o Aplicativo Android/Desktop (1 a 5000).
// MIN_PERIODO=0 permitia um chase degenerado de 0 ms: com TestPeriodo=0 o
// Timer0 avançava os 32 LEDs em ~32 ms e o "acerto" virava "aperte
// imediatamente ao iniciar o teste".
#define MIN_PERIODO 5
#define MAX_PERIODO 5000
#define PERIODO_STEP 5

i16 TestPeriodo = 100;

#define NUM_LEDS 32
u8 CurrentLed = 0;

// LED alvo fixo (numeração de 1 a NUM_LEDS)
#define TARGET_LED_NUMBER 25
#define TARGET_LED_INDEX  (TARGET_LED_NUMBER - 1)

// Calibração do medidor de bateria, extraída de bancada em
// "Docs/docs novo/Calibração medidor carga bateria.xlsx": ADC bruto de 12 bits
// (AN0) medido a bateria vazia (0%) e cheia (100%). Regressão linear direta
// sobre o valor cru do ADC, sem depender de assumir a razão do divisor
// resistivo — a versão anterior assumia um divisor 2:1 (multiplicador *2.0)
// que não bate com a razão real medida (~0.41741, ou seja, multiplicador
// ~2.396), fazendo o indicador subestimar a carga real.
#define BATTERY_ADC_EMPTY 2112
#define BATTERY_ADC_FULL  2980

// Arredonda a porcentagem para o degrau de 5% mais próximo em vez de truncar
// para o inteiro abaixo. Isso resolve dois sintomas do mesmo problema: (1) o
// indicador "piscando" entre N% e N-1% por causa de ruído de poucos contagens
// do ADC perto de um limiar de 1%, e (2) a bateria cheia nunca mostrar 100%
// por a leitura raramente cruzar o limiar exato de BATTERY_ADC_FULL — com o
// arredondamento, toda uma faixa de leituras perto do topo (ou do fundo) cai
// no mesmo degrau de 100% (ou 0%) em vez de depender de um limiar exato.
#define BATTERY_PERCENT_STEP 5

// Variáveis para comunicação Bluetooth, agora escritas pela ISR de recepção
// do UART1 (ver interrupt()) e lidas pelo loop principal (ver
// check_bluetooth()) — precisam ser volatile para o compilador não assumir
// que só o código "de cima" as modifica.
volatile char bl_buffer[10];
volatile u8 bl_idx = 0;
volatile u8 bl_receiving = 0;
// Setada pela ISR quando um frame '<...>' completo chega; consumida pelo
// loop principal, que faz o atoi()/UART1_Write() do ack fora da ISR (esses
// UART1_Write bloqueiam ~1ms por byte a 9600 baud — não é algo que se queira
// rodar dentro de uma interrupção).
volatile u8 bl_frame_ready = 0;

// Liga/desliga o módulo Bluetooth para economizar bateria (ligado por padrão)
u8 BluetoothEnabled = 1;

void ApplyBluetoothState() {
    BLUETOOTH_CONTROL_PORT = BluetoothEnabled ? BLUETOOTH_ON_LEVEL : BLUETOOTH_OFF_LEVEL;
}

// ---------------- BATERIA DE TESTES ALEATÓRIOS ("Aleatorio") ----------------
//
// O menu "Aleatorio" roda N testes de cada um de até M valores de período
// escolhidos pelo usuário (M <= MAX_BATTERY_VALUES), em ordem embaralhada, e
// no final mostra a média de desvio em relação ao alvo. MAX_BATTERY_VALUES é
// pequeno de propósito: o PIC18F4423 só tem 768 bytes de RAM no total (ver
// datasheet), e cada valor extra também significa mais uma tela do
// assistente para o usuário preencher à mão.
#define MAX_BATTERY_VALUES  8
#define MAX_BATTERY_REPEATS 99

// Config "oficial" (já confirmada e persistida em EEPROM) vs. "pendente"
// (sendo editada no assistente, ver STATE_RANDOM_CONFIG_*): só vira oficial
// quando o assistente termina (ver CommitBatteryConfig()), para que cancelar
// no meio (ver AleatorioOnClick()/BatteryValueCount==0) nunca estrague uma
// configuração já salva.
u8  BatteryValueCount = 0;        // M: quantos valores distintos
u8  BatteryRepeatsPerValue = 10;  // N: repetições por valor
i16 BatteryValues[MAX_BATTERY_VALUES];

u8  PendingValueCount;
u8  PendingRepeatsPerValue;
i16 PendingValues[MAX_BATTERY_VALUES];
u8  RandomConfigValueIndex = 0;   // qual valor (0..PendingValueCount-1) está sendo editado agora

// Estado de uma bateria em andamento/concluída. BatteryActive distingue o
// caminho "bateria" do caminho normal de teste único exatamente nos dois
// pontos em que os dois fluxos se tocam: STATE_STARTING_TEST (texto exibido)
// e STATE_CALCULATE_TEST_RESULT (o que acontece depois de calcular o
// resultado). O resto da máquina de teste (TEST_READY/TEST_BEGIN/
// RUNNING_TEST) não muda em nada entre os dois modos.
volatile u8 BatteryActive = 0;
u8  BatteryRemaining[MAX_BATTERY_VALUES]; // repetições restantes de cada valor
u16 BatteryTestsTotal = 0;                // M*N
u16 BatteryTestsRemaining = 0;
i32 BatterySumSigned = 0; // soma de ReactionTimeDifference (revela viés cedo/atrasado)
i32 BatterySumAbs = 0;    // soma de |ReactionTimeDifference| (o quão longe do alvo, em média)
u8  BatteryResultSelection = 0; // tela final: 0 = "Repetir", 1 = "Menu"

// ---- PRNG ----
// O PIC não tem gerador de números aleatórios por hardware nem um pino
// analógico flutuante sobrando para colher ruído. Para embaralhar a ordem
// dos testes isso é suficiente: não precisa ser criptograficamente
// aleatório, só difícil do testador prever. FreeRunningTicks conta a cada
// 1ms (ver interrupt()) independente do estado do programa, então o
// instante exato em que o usuário clica para confirmar o último valor do
// assistente é uma semente de fato imprevisível.
volatile u16 FreeRunningTicks = 0;
u32 RandomSeedState = 1;

void SeedRandom(u16 seed) {
    RandomSeedState = seed ? seed : 1;
}

u16 NextRandom() {
    // LCG (parâmetros clássicos de Numerical Recipes); descarta os bits
    // baixos, que num LCG são os menos aleatórios.
    RandomSeedState = RandomSeedState * 1103515245UL + 12345UL;
    return (u16)(RandomSeedState >> 8);
}

// ---- Persistência em EEPROM ----
// Layout: [0]=byte mágico, [1]=M, [2]=N, [3..] = MAX_BATTERY_VALUES valores
// de 16 bits (little-endian). O byte mágico é gravado por último em
// SaveBatteryConfigToEEPROM() de propósito: se a energia cair no meio da
// gravação, uma leitura seguinte vê o mágico antigo (ou nenhum, numa EEPROM
// de fábrica) e descarta tudo em vez de confiar num config parcialmente
// escrito.
#define EEPROM_MAGIC_ADDR   0
#define EEPROM_MAGIC_VALUE  0xB7
#define EEPROM_COUNT_ADDR   1
#define EEPROM_REPEATS_ADDR 2
#define EEPROM_VALUES_ADDR  3

void SaveBatteryConfigToEEPROM() {
    u8 i;
    EEPROM_Write(EEPROM_COUNT_ADDR, BatteryValueCount);
    EEPROM_Write(EEPROM_REPEATS_ADDR, BatteryRepeatsPerValue);
    for (i = 0; i < MAX_BATTERY_VALUES; i++) {
        EEPROM_Write(EEPROM_VALUES_ADDR + i * 2,     (u8)(BatteryValues[i] & 0xFF));
        EEPROM_Write(EEPROM_VALUES_ADDR + i * 2 + 1, (u8)((BatteryValues[i] >> 8) & 0xFF));
    }
    EEPROM_Write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
}

void LoadBatteryConfigFromEEPROM() {
    u8 i, lo, hi;

    if (EEPROM_Read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
        // Nunca configurado (ou EEPROM "de fábrica", toda 0xFF) — usa padrões
        // em vez de decodificar lixo.
        BatteryValueCount = 0;
        BatteryRepeatsPerValue = 10;
        for (i = 0; i < MAX_BATTERY_VALUES; i++) BatteryValues[i] = TestPeriodo;
        return;
    }

    BatteryValueCount = EEPROM_Read(EEPROM_COUNT_ADDR);
    if (BatteryValueCount > MAX_BATTERY_VALUES) BatteryValueCount = MAX_BATTERY_VALUES;

    BatteryRepeatsPerValue = EEPROM_Read(EEPROM_REPEATS_ADDR);
    if (BatteryRepeatsPerValue < 1 || BatteryRepeatsPerValue > MAX_BATTERY_REPEATS) {
        BatteryRepeatsPerValue = 10;
    }

    for (i = 0; i < MAX_BATTERY_VALUES; i++) {
        lo = EEPROM_Read(EEPROM_VALUES_ADDR + i * 2);
        hi = EEPROM_Read(EEPROM_VALUES_ADDR + i * 2 + 1);
        BatteryValues[i] = (i16)(((u16)hi << 8) | lo);
        if (BatteryValues[i] < MIN_PERIODO || BatteryValues[i] > MAX_PERIODO) {
            BatteryValues[i] = TestPeriodo;
        }
    }
}

// Clique em "Aleatorio": copia o último config confirmado (vindo da EEPROM
// no boot, ou do assistente anterior) para as variáveis "pendentes" do
// assistente — reabrir o assistente começa de onde parou, em vez de do
// zero — e entra no primeiro passo (quantidade de valores).
void AleatorioOnClick() {
    u8 i;
    PendingValueCount = BatteryValueCount;
    PendingRepeatsPerValue = BatteryRepeatsPerValue;
    for (i = 0; i < MAX_BATTERY_VALUES; i++) {
        PendingValues[i] = BatteryValues[i];
    }
    ProgramState = STATE_INIT_RANDOM_CONFIG_COUNT;
}

// Período e Bluetooth são editados in-line na própria tela de menu (ver
// renderMenu()) em vez de abrirem uma tela própria: um clique no item
// "trava" a rotação do encoder nesse item (em vez de navegar entre os
// itens), o segundo clique destrava. Os dois itens só precisam alternar
// essa trava — ToggleMenuLock() fica genérico para ambos.
volatile u8 MenuLocked = 0;

// Cursor exibido na frente do item selecionado: ">" navegando entre itens,
// "]" com o item travado para edição (ver MenuLocked).
#define MENU_CURSOR_UNLOCKED ">"
#define MENU_CURSOR_LOCKED   "]"

// Menu OnClicks
void ToggleMenuLock() {
    MenuLocked = !MenuLocked;
}

void IniciarOnClick() {
    ProgramState = STATE_STARTING_TEST;
}

typedef void (*OnClickFunc)(void);
typedef struct { StrView Name; OnClickFunc OnClick; } MenuOption;

#define NUM_MENU_ITEMS 4
#define MENU_ITEM_PERIODO   0
#define MENU_ITEM_BLUETOOTH 1
#define MENU_ITEM_ALEATORIO 2
#define MENU_ITEM_INICIAR   3

const MenuOption MenuItems[NUM_MENU_ITEMS] = {
    { MAKE_VIEW("Periodo"),   ToggleMenuLock },
    { MAKE_VIEW("Bluetooth"), ToggleMenuLock },
    { MAKE_VIEW("Aleatorio"), AleatorioOnClick },
    { MAKE_VIEW("Iniciar"),   IniciarOnClick }
};

i8 SelectedMenuOption = 0;
volatile i8 EncoderInput = 0;

i8 getEncoderInput() {
    i8 n = EncoderInput;
    EncoderInput = 0;
    return n;
}

u16 LedExposition = 0;                      
volatile u16 TimeSinceTestStarted = 0;      
volatile u16 TimeMeantForUserReaction = 0;  
volatile i32 ReactionTimeDifference = 0;    

void ReloadTimer0() {
    TMR0H = TIMER0_LOAD_HIGH;
    TMR0L = TIMER0_LOAD_LOW;
}
void PauseTimer0() { GIE_bit = 0; }
void UnpauseTimer0() { GIE_bit = 1; }

u16 Read_ADC_Manual() {
    ADCON0 = 0x01;
    Delay_us(20);
    GO_DONE_bit = 1;
    while (GO_DONE_bit == 1);
    return (((unsigned int)ADRESH << 8) | ADRESL);
}

// ---------------- BLUETOOTH FUNCTIONS ----------------

// Envia os dados como string ASCII no formato <Valor> para os Apps
void bl_send_reaction_time(i32 reaction_time) {
    char out_buffer[15];
    LongToStr(reaction_time, out_buffer);
    Ltrim(out_buffer); // Remove espaços em branco
    
    UART1_Write('<');
    UART1_Write_Text(out_buffer);
    UART1_Write('>');
}

// Confirma para o app se o novo período foi aplicado ('A') ou ignorado porque
// um teste está em andamento ('B'). Um único caractere evita qualquer
// ambiguidade com o payload numérico de bl_send_reaction_time().
void bl_send_period_ack(u8 applied) {
    UART1_Write('<');
    UART1_Write(applied ? 'A' : 'B');
    UART1_Write('>');
}

// Um teste em andamento já calculou TimeMeantForUserReaction a partir do
// TestPeriodo vigente; aceitar um novo valor nesse meio tempo desincroniza o
// cálculo do tempo de reação da velocidade real do chaser.
u8 IsTestInProgress() {
    switch (ProgramState) {
        case STATE_STARTING_TEST:
        case STATE_TEST_READY:
        case STATE_TEST_BEGIN:
        case STATE_RUNNING_TEST:
        case STATE_CALCULATE_TEST_RESULT:
            return 1;
        default:
            return 0;
    }
}

// Chamada pela ISR de recepção do UART1 (ver interrupt()) a cada byte
// recebido. Só acumula no buffer e sinaliza bl_frame_ready — nunca faz
// atoi()/UART1_Write() aqui dentro, já que isso bloquearia a interrupção por
// vários ms (ver comentário de bl_frame_ready).
void bl_handle_rx_byte(char byte) {
    if (byte == '<') {
        bl_receiving = 1;
        bl_idx = 0;
    } else if (byte == '>') {
        bl_receiving = 0;
        bl_buffer[bl_idx] = '\0';
        bl_frame_ready = 1;
    } else if (bl_receiving && bl_idx < 9) {
        bl_buffer[bl_idx++] = byte;
    }
}

// Chamada pelo loop principal a cada iteração: processa (fora da ISR) o
// último frame '<...>' completo sinalizado por bl_handle_rx_byte(), se
// houver algum pendente.
void check_bluetooth() {
    char local_buffer[10];

    if (!bl_frame_ready) return;

    // Copia o buffer volatile para uma cópia local antes de processar, para
    // não correr risco de a ISR começar a sobrescrevê-lo com o próximo frame
    // no meio do atoi()/UART1_Write() abaixo.
    strcpy(local_buffer, (char*)bl_buffer);
    bl_frame_ready = 0;

    // Ignora o novo período durante um teste em andamento (ver
    // IsTestInProgress) para não desincronizar o resultado; o pacote ainda é
    // consumido normalmente para não travar o parser.
    if (!IsTestInProgress()) {
        // Converte o pacote string ASCII recebido pelo app em numérico
        TestPeriodo = atoi(local_buffer);

        // Aplica os Clampings
        if (TestPeriodo < MIN_PERIODO) TestPeriodo = MIN_PERIODO;
        if (TestPeriodo > MAX_PERIODO) TestPeriodo = MAX_PERIODO;

        bl_send_period_ack(1);
    } else {
        bl_send_period_ack(0);
    }
}

// ---------------- INTERRUPTS ----------------

void interrupt() {
    u16 current_timer;

    if(TMR0IF_bit) {
        TMR0IF_bit  = 0x00;
        ReloadTimer0();
        FreeRunningTicks++; // ver comentário de FreeRunningTicks (semente do PRNG da bateria aleatória)

#ifdef TIMER0_CALIBRATION_TEST
        LATB5_bit = ~LATB5_bit;
#endif

        if(ProgramState == STATE_RUNNING_TEST) {
            LedExposition++;
            TimeSinceTestStarted++;

            if(LedExposition >= TestPeriodo) {
                LedExposition = 0;
                CurrentLed++;
                if (CurrentLed >= NUM_LEDS) {
                    CurrentLed = 0;
                }
                SET_LED_DMUX(CurrentLed);
            }
        }
    }

    // Recepção do UART1 por interrupção em vez de polling: check_bluetooth()
    // sendo chamado só uma vez por iteração do loop principal (que faz várias
    // escritas de LCD + leitura de ADC por volta em renderMenu()) deixava
    // brechas grandes o bastante para um pacote <period_ms> nunca ser lido a
    // tempo. A interrupção lê cada byte em poucos microssegundos, não importa
    // o que o loop principal esteja fazendo. RCIE é desligado durante um
    // teste em andamento (ver STATE_TEST_BEGIN/STATE_CALCULATE_TEST_RESULT)
    // para não haver risco de essa interrupção atrasar a base de tempo do
    // Timer0 durante a medição da reação.
    if (RCIF_bit) {
        if (OERR_bit) {
            CREN_bit = 0;
            CREN_bit = 1;
        } else {
            while (UART1_Data_Ready()) {
                bl_handle_rx_byte(UART1_Read());
            }
        }
    }

    if(INT0IF_bit) {
        INT0IF_bit = 0x00;
        switch (ProgramState) {
            case STATE_TEST_READY:
                ProgramState = STATE_TEST_BEGIN;
                break;
            case STATE_RUNNING_TEST:
                current_timer = TimeSinceTestStarted;
                ReactionTimeDifference = (i32)current_timer - (i32)TimeMeantForUserReaction;
                ProgramState = STATE_CALCULATE_TEST_RESULT;
                break;
        }
    }

    if(INT1IF_bit) {
        INT1IF_bit = 0x00;
        if(ENCODER_SIGNAL_PORT == 1) EncoderInput++;
        else EncoderInput--;
    }

    if(INT2IF_bit) {
        INT2IF_bit = 0x00;
        switch(ProgramState) {
            case STATE_FINISHED_TEST:
                ProgramState = STATE_INIT_RENDER_MENU;
            break;
            case STATE_SELECTING_MENU:
                MenuItems[SelectedMenuOption].OnClick();
            break;
            // Assistente da bateria aleatória: cada clique confirma o passo
            // atual e avança para o próximo (ver comentário dos estados
            // STATE_RANDOM_CONFIG_* / STATE_INIT_RANDOM_*). Só a transição
            // de estado acontece aqui dentro da ISR — gravar na EEPROM e
            // sortear a ordem dos testes são operações "pesadas" demais
            // para uma interrupção, então ficam em STATE_INIT_RANDOM_BATTERY
            // / STATE_INIT_RANDOM_BATTERY_REPEAT, tratadas no loop principal.
            case STATE_RANDOM_CONFIG_COUNT:
                if (PendingValueCount == 0) {
                    ProgramState = STATE_INIT_RENDER_MENU; // M=0 é "Cancelar"
                } else {
                    ProgramState = STATE_INIT_RANDOM_CONFIG_REPEATS;
                }
            break;
            case STATE_RANDOM_CONFIG_REPEATS:
                RandomConfigValueIndex = 0;
                ProgramState = STATE_INIT_RANDOM_CONFIG_VALUE;
            break;
            case STATE_RANDOM_CONFIG_VALUE:
                RandomConfigValueIndex++;
                if (RandomConfigValueIndex >= PendingValueCount) {
                    ProgramState = STATE_INIT_RANDOM_BATTERY;
                } else {
                    ProgramState = STATE_INIT_RANDOM_CONFIG_VALUE;
                }
            break;
            case STATE_RANDOM_BATTERY_RESULT:
                ProgramState = (BatteryResultSelection == 0)
                    ? STATE_INIT_RANDOM_BATTERY_REPEAT
                    : STATE_INIT_RENDER_MENU;
            break;
        }
    }
}

void strcpy_ROM_to_RAM(char* ram_dest, const char* rom_src) {
    char c;
    while (c = *rom_src++) {
        *ram_dest++ = c;
    }
    *ram_dest = '\0';
}

// Preenche buf (já terminado em '\0') com espaços até width colunas, para
// apagar na LCD o que tiver sobrado de uma renderização anterior mais longa
// (ex.: TestPeriodo encolhendo de "5000" para "5" precisa apagar 3
// colunas) — sem isso o Lcd_Out só escreveria até o '\0' atual e deixaria
// os caracteres antigos visíveis depois do novo texto, mais curto.
void PadToWidth(char* buf, u8 width) {
    u8 len = strlen(buf);
    while (len < width) buf[len++] = ' ';
    buf[len] = '\0';
}

// Aplica a rotação do encoder ao item travado (ver MenuLocked) em vez de
// navegar entre itens: Período ajusta o valor em passos de PERIODO_STEP,
// Bluetooth alterna ligado/desligado a cada giro. Mesma lógica que antes
// vivia em renderPeriodoMenu()/renderBluetoothMenu(), só que operando
// in-line na linha do item em vez de numa tela própria.
void AdjustSelectedMenuValue(i8 encoder_delta) {
    switch (SelectedMenuOption) {
        case MENU_ITEM_PERIODO:
            TestPeriodo += encoder_delta * PERIODO_STEP;
            if (TestPeriodo > MAX_PERIODO) TestPeriodo = MIN_PERIODO;
            if (TestPeriodo < MIN_PERIODO) TestPeriodo = MAX_PERIODO;
            break;
        case MENU_ITEM_BLUETOOTH:
            if (encoder_delta != 0) {
                BluetoothEnabled = !BluetoothEnabled;
                ApplyBluetoothState();
            }
            break;
        default:
            break;
    }
}

void renderMenu() {
    u8 i;
    char lcd_line_buffer[LCD_COLLUMN_COUNT];
    char value_buffer[7];
    i8 encoder_delta;
    float raw_percent;
    i16 rounded_percent;
    volatile u32 adc_value;
    u8 percent;

    encoder_delta = getEncoderInput();

    if (MenuLocked) {
        AdjustSelectedMenuValue(encoder_delta);
    } else {
        SelectedMenuOption += encoder_delta;
        if(SelectedMenuOption < 0) SelectedMenuOption = NUM_MENU_ITEMS - 1;
        else if(SelectedMenuOption >= NUM_MENU_ITEMS) SelectedMenuOption = 0;
    }

    for (i = 0; i < NUM_MENU_ITEMS; i++) {
        switch (i) {
            case MENU_ITEM_PERIODO:
                strcpy(lcd_line_buffer, "Periodo: ");
                IntToStr(TestPeriodo, value_buffer);
                Ltrim(value_buffer);
                strcat(lcd_line_buffer, value_buffer);
                strcat(lcd_line_buffer, " ms");
                PadToWidth(lcd_line_buffer, 16);
                break;
            case MENU_ITEM_BLUETOOTH:
                strcpy(lcd_line_buffer, "Bluetooth: ");
                strcat(lcd_line_buffer, BluetoothEnabled ? "Ligado" : "Deslig.");
                PadToWidth(lcd_line_buffer, 18);
                break;
            default:
                memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
                strcpy_ROM_to_RAM(lcd_line_buffer, MenuItems[i].Name.Data);
                break;
        }

        Lcd_Out(i+1, 2, lcd_line_buffer);

        if (i == SelectedMenuOption) {
            Lcd_Out(i+1, 1, MenuLocked ? MENU_CURSOR_LOCKED : MENU_CURSOR_UNLOCKED);
        } else {
            Lcd_Out(i+1, 1, " ");
        }
    }

    adc_value = Read_ADC_Manual();
    raw_percent = (100.0 / (BATTERY_ADC_FULL - BATTERY_ADC_EMPTY)) *
                  ((float)adc_value - BATTERY_ADC_EMPTY);

    // Arredonda (não trunca) para o degrau de BATTERY_PERCENT_STEP mais
    // próximo antes de fazer o clamp — ver comentário de BATTERY_PERCENT_STEP.
    rounded_percent = (i16)((raw_percent / BATTERY_PERCENT_STEP) + 0.5) * BATTERY_PERCENT_STEP;

    if (rounded_percent >= 100) percent = 100;
    else if (rounded_percent <= 0) percent = 0;
    else percent = (u8)rounded_percent;

    IntToStr(percent, lcd_line_buffer);
    Ltrim(lcd_line_buffer);
    Lcd_Out(4, (LCD_COLLUMN_COUNT - 3), lcd_line_buffer);
    Lcd_Out(4, LCD_COLLUMN_COUNT, "%");
}

// ---- Telas do assistente "Aleatorio" (STATE_RANDOM_CONFIG_*) ----
// Mesmo padrão de Período/Bluetooth: o encoder ajusta o campo, o clique (na
// ISR, ver interrupt()) confirma e avança. Cada uma destas é redesenhada a
// cada volta do loop principal sem Lcd_Cmd(_LCD_CLEAR) — só o texto que de
// fato varia é reescrito, usando PadToWidth() para apagar sobras de um
// valor mais longo; o estado STATE_INIT_* que antecede cada uma já limpou a
// tela uma vez na entrada (os rótulos fixos mudam de tamanho de uma tela
// para outra).

void renderBatteryCountConfig() {
    char line_buffer[LCD_COLLUMN_COUNT];
    i8 new_count;

    new_count = (i8)PendingValueCount + getEncoderInput();
    if (new_count < 0) new_count = MAX_BATTERY_VALUES;
    else if (new_count > MAX_BATTERY_VALUES) new_count = 0;
    PendingValueCount = (u8)new_count;

    Lcd_Out(1, 1, "Aleatorio: qtd. de");
    Lcd_Out(2, 1, "valores");

    if (PendingValueCount == 0) {
        strcpy(line_buffer, "0 (cancelar)");
    } else {
        IntToStr(PendingValueCount, line_buffer);
        Ltrim(line_buffer);
    }
    PadToWidth(line_buffer, LCD_COLLUMN_COUNT - 1);
    Lcd_Out(3, 1, line_buffer);

    Lcd_Out(4, 1, "Clique p/ confirmar");
}

void renderBatteryRepeatsConfig() {
    char line_buffer[LCD_COLLUMN_COUNT];
    i16 new_repeats;

    new_repeats = (i16)PendingRepeatsPerValue + getEncoderInput();
    if (new_repeats < 1) new_repeats = MAX_BATTERY_REPEATS;
    else if (new_repeats > MAX_BATTERY_REPEATS) new_repeats = 1;
    PendingRepeatsPerValue = (u8)new_repeats;

    Lcd_Out(1, 1, "Testes por valor:");

    IntToStr(PendingRepeatsPerValue, line_buffer);
    Ltrim(line_buffer);
    PadToWidth(line_buffer, LCD_COLLUMN_COUNT - 1);
    Lcd_Out(2, 1, line_buffer);

    Lcd_Out(4, 1, "Clique p/ confirmar");
}

void renderBatteryValueConfig() {
    char line_buffer[LCD_COLLUMN_COUNT];
    char num_buffer[7];
    i16 current;

    current = PendingValues[RandomConfigValueIndex] + getEncoderInput() * PERIODO_STEP;
    if (current > MAX_PERIODO) current = MIN_PERIODO;
    if (current < MIN_PERIODO) current = MAX_PERIODO;
    PendingValues[RandomConfigValueIndex] = current;

    strcpy(line_buffer, "Valor ");
    IntToStr(RandomConfigValueIndex + 1, num_buffer);
    Ltrim(num_buffer);
    strcat(line_buffer, num_buffer);
    strcat(line_buffer, " de ");
    IntToStr(PendingValueCount, num_buffer);
    Ltrim(num_buffer);
    strcat(line_buffer, num_buffer);
    strcat(line_buffer, ":");
    PadToWidth(line_buffer, LCD_COLLUMN_COUNT - 1);
    Lcd_Out(1, 1, line_buffer);

    IntToStr(current, num_buffer);
    Ltrim(num_buffer);
    strcpy(line_buffer, num_buffer);
    strcat(line_buffer, " ms");
    PadToWidth(line_buffer, LCD_COLLUMN_COUNT - 1);
    Lcd_Out(2, 1, line_buffer);

    Lcd_Out(4, 1, "Clique p/ confirmar");
}

// Tela final da bateria: mostra os dois agregados (desvio médio absoluto e
// médio com sinal) e um seletor de 2 opções na última linha — o encoder
// alterna entre "Repetir"/"Menu" (girar em qualquer direção já que só há 2
// opções) e o clique (ver interrupt()) confirma a escolha.
void renderBatteryResult() {
    char line_buffer[LCD_COLLUMN_COUNT];
    char num_buffer[12];
    i8 encoder_delta;

    encoder_delta = getEncoderInput();
    if (encoder_delta != 0) BatteryResultSelection = !BatteryResultSelection;

    Lcd_Out(1, 1, "Bateria concluida");

    strcpy(line_buffer, "Media: ");
    LongToStr(BatterySumSigned / (i32)BatteryTestsTotal, num_buffer);
    Ltrim(num_buffer);
    strcat(line_buffer, num_buffer);
    strcat(line_buffer, " ms");
    PadToWidth(line_buffer, LCD_COLLUMN_COUNT - 1);
    Lcd_Out(2, 1, line_buffer);

    strcpy(line_buffer, "Desvio abs: ");
    LongToStr(BatterySumAbs / (i32)BatteryTestsTotal, num_buffer);
    Ltrim(num_buffer);
    strcat(line_buffer, num_buffer);
    strcat(line_buffer, " ms");
    PadToWidth(line_buffer, LCD_COLLUMN_COUNT - 1);
    Lcd_Out(3, 1, line_buffer);

    if (BatteryResultSelection == 0) {
        Lcd_Out(4, 1, ">Repetir");
        Lcd_Out(4, 12, " Menu");
    } else {
        Lcd_Out(4, 1, " Repetir");
        Lcd_Out(4, 12, ">Menu");
    }
}

// ---- Execução da bateria ----

void CommitBatteryConfig() {
    u8 i;
    BatteryValueCount = PendingValueCount;
    BatteryRepeatsPerValue = PendingRepeatsPerValue;
    for (i = 0; i < MAX_BATTERY_VALUES; i++) {
        BatteryValues[i] = PendingValues[i];
    }
    SaveBatteryConfigToEEPROM();
}

// Sorteio ponderado sem reposição: escolhe o índice i com probabilidade
// proporcional a BatteryRemaining[i] dentre o total ainda restante
// (BatteryTestsRemaining) — garante que, no fim, cada valor tenha rodado
// exatamente BatteryRepeatsPerValue vezes, sem precisar guardar a sequência
// inteira de M*N testes (ver comentário de MAX_BATTERY_VALUES sobre RAM).
u8 PickNextBatteryValueIndex() {
    u16 r, running;
    u8 i;

    r = NextRandom() % BatteryTestsRemaining;
    running = 0;
    for (i = 0; i < BatteryValueCount; i++) {
        running += BatteryRemaining[i];
        if (r < running) return i;
    }
    return BatteryValueCount - 1; // nunca deveria chegar aqui
}

// Sorteia e reserva o valor do próximo sub-teste (decrementando sua conta de
// repetições restantes e o total restante) e já o aplica em TestPeriodo,
// que STATE_STARTING_TEST lê normalmente. Chamada uma vez ao iniciar a
// bateria (ResetBatteryRun()) e mais uma vez após cada sub-teste enquanto
// sobrarem testes (ver STATE_CALCULATE_TEST_RESULT).
void AdvanceToNextBatteryValue() {
    u8 idx = PickNextBatteryValueIndex();
    BatteryRemaining[idx]--;
    BatteryTestsRemaining--;
    TestPeriodo = BatteryValues[idx];
}

// Zera os contadores de uma bateria (nova ou repetida) e sorteia o primeiro
// valor. Não grava EEPROM — isso só acontece ao sair do assistente (ver
// CommitBatteryConfig()), já que repetir a mesma bateria não muda a
// configuração salva.
void ResetBatteryRun() {
    u8 i;
    for (i = 0; i < BatteryValueCount; i++) {
        BatteryRemaining[i] = BatteryRepeatsPerValue;
    }
    BatteryTestsTotal = (u16)BatteryValueCount * (u16)BatteryRepeatsPerValue;
    BatteryTestsRemaining = BatteryTestsTotal;
    BatterySumSigned = 0;
    BatterySumAbs = 0;
    BatteryActive = 1;

    SeedRandom((u16)Read_ADC_Manual() ^ FreeRunningTicks);

    AdvanceToNextBatteryValue();
}

void main() {
    char lcd_line_buffer[LCD_COLLUMN_COUNT];    
    char conversions_buffer[15];                

    RCON.IPEN = 0;                              

    CMCON = 0x07;                               
    T0CON = 0x88;                               
    ReloadTimer0();

    ADCON1  = 0x0F;                             
    INTCON  = 0xF0;                             

    INTEDG0_bit = 0x00;                         
    INTEDG1_bit = 0x00;                         
    INTEDG2_bit = 0x01;                         
    RBPU_bit = 0;          

    INT0IE_bit  = 0x01;                         
    INT1IE_bit  = 0x01;                         
    INT2IE_bit  = 0x01;                         

    TRISB   = 0xFF;

#ifdef TIMER0_CALIBRATION_TEST
    TRISB.B5 = 0x00;
#endif

    TRISD   = 0x00;
    TRISE.B2= 0x00;
    LATE.B2 = 0;

    // RWM BUG FIX: Inicializa e limpa toda a LATC usando o registrador Latch.
    TRISC = 0x00;
    LATC = 0x00;

    // UART1 RX (RC7) precisa ficar como ENTRADA para o receptor funcionar —
    // a biblioteca UART1_Init() do mikroC só força o TX (RC6) para saída;
    // ela não reconfigura o TRIS da linha RX. Deixado como saída (herdado do
    // TRISC=0x00 acima), o pino é ativamente dirigido pelo próprio latch do
    // PIC e nunca enxerga o sinal vindo do HC-05 — a transmissão continua
    // funcionando normalmente porque RC6 já precisa ser saída mesmo.
    TRISC.B7 = 1;

    ADCON1 = 0x0E;
    ADCON2 = 0b10100101;    
    TRISA.B0 = 1;           

    Lcd_Init();
    Lcd_Cmd(_LCD_CLEAR);               
    Lcd_Cmd(_LCD_CURSOR_OFF);          

    UART1_Init(9600);
    Delay_ms(100);

    // Recepção do UART1 passa a ser por interrupção (ver interrupt()) em vez
    // de polling — PEIE_bit já está ligado via INTCON=0xF0 acima. Desligado
    // durante um teste em andamento (ver STATE_TEST_BEGIN/
    // STATE_CALCULATE_TEST_RESULT) para não arriscar jitter na base de tempo
    // do Timer0 durante a medição da reação.
    RCIF_bit = 0;
    RCIE_bit = 1;

    ApplyBluetoothState();
    LoadBatteryConfigFromEEPROM();

    while(1) {
        // Escuta constantemente atualizações Bluetooth do Android/Desktop
        check_bluetooth();

        switch(ProgramState) {
            case STATE_IDLE:
                ProgramState = STATE_INIT_RENDER_MENU;
            break;
            case STATE_INIT_RENDER_MENU:
                Lcd_Cmd(_LCD_CLEAR);
                MenuLocked = 0;
                BatteryActive = 0;
                ProgramState = STATE_SELECTING_MENU;
            break;
            case STATE_SELECTING_MENU:
                renderMenu();
            break;
            case STATE_INIT_RANDOM_CONFIG_COUNT:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_RANDOM_CONFIG_COUNT;
            break;
            case STATE_RANDOM_CONFIG_COUNT:
                renderBatteryCountConfig();
            break;
            case STATE_INIT_RANDOM_CONFIG_REPEATS:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_RANDOM_CONFIG_REPEATS;
            break;
            case STATE_RANDOM_CONFIG_REPEATS:
                renderBatteryRepeatsConfig();
            break;
            case STATE_INIT_RANDOM_CONFIG_VALUE:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_RANDOM_CONFIG_VALUE;
            break;
            case STATE_RANDOM_CONFIG_VALUE:
                renderBatteryValueConfig();
            break;
            case STATE_INIT_RANDOM_BATTERY:
                CommitBatteryConfig();
                ResetBatteryRun();
                ProgramState = STATE_STARTING_TEST;
            break;
            case STATE_INIT_RANDOM_BATTERY_REPEAT:
                ResetBatteryRun();
                ProgramState = STATE_STARTING_TEST;
            break;
            case STATE_RANDOM_BATTERY_RESULT:
                renderBatteryResult();
            break;
            case STATE_STARTING_TEST:
                TMR0IE_bit = 0;
                Lcd_Cmd(_LCD_CLEAR);

                if (BatteryActive) {
                    Lcd_Out(1, 1, "Bateria aleatoria");

                    strcpy(lcd_line_buffer, "Teste ");
                    IntToStr(BatteryTestsTotal - BatteryTestsRemaining, conversions_buffer);
                    Ltrim(conversions_buffer);
                    strcat(lcd_line_buffer, conversions_buffer);
                    strcat(lcd_line_buffer, " de ");
                    IntToStr(BatteryTestsTotal, conversions_buffer);
                    Ltrim(conversions_buffer);
                    strcat(lcd_line_buffer, conversions_buffer);
                    Lcd_Out(2, 1, lcd_line_buffer);

                    Lcd_Out(3, 1, "Aperte o botao de");
                    Lcd_Out(4, 1, "teste para comecar");
                } else {
                    Lcd_Out(1, 1, "Teste pronto.");
                    Lcd_Out(2, 1, "Aperte o botao de teste");
                    Lcd_Out(3, 1, "para comecar");
                }

                LedExposition = 0;
                TimeSinceTestStarted = 0;
                TimeMeantForUserReaction = TARGET_LED_INDEX * TestPeriodo;
                SET_LED_DMUX(0);
                CurrentLed = 0;
                
                ReloadTimer0();
                ProgramState = STATE_TEST_READY;
            break;
            case STATE_TEST_READY:
            break;
            case STATE_TEST_BEGIN:
                TMR0IE_bit = 1;

                // Desliga a interrupção de recepção do UART1 enquanto o teste
                // roda (ver comentário em main()/interrupt()) — nenhum pacote
                // é aceito nesse intervalo mesmo (ver IsTestInProgress()), e
                // isso garante que a ISR do UART1 não possa competir por
                // tempo de CPU com a base de tempo do Timer0 durante a
                // medição da reação. Bytes que chegarem nesse meio tempo só
                // ficam pendentes no buffer de hardware (2 bytes) até serem
                // religados abaixo, em STATE_CALCULATE_TEST_RESULT.
                RCIE_bit = 0;

                Lcd_Cmd(_LCD_CLEAR);
                Lcd_Out(1, 1, "Testando...");
                ProgramState = STATE_RUNNING_TEST;
            break;
            case STATE_RUNNING_TEST:
            break;
            case STATE_CALCULATE_TEST_RESULT:
                PauseTimer0();

                // Envia o Resultado para o App via Bluetooth — em ambos os
                // modos: numa bateria, cada sub-teste ainda é enviado em
                // tempo real como sempre foi, o app só ainda não sabe que
                // faz parte de uma bateria (ver Docs/dev/FIRMWARE.md).
                bl_send_reaction_time(ReactionTimeDifference);

                Lcd_Cmd(_LCD_CLEAR);
                Delay_ms(5);

                if (BatteryActive) {
                    BatterySumSigned += ReactionTimeDifference;
                    BatterySumAbs += (ReactionTimeDifference < 0)
                        ? -ReactionTimeDifference
                        : ReactionTimeDifference;

                    if (BatteryTestsRemaining > 0) {
                        // Ainda há testes a fazer: sorteia o próximo valor e
                        // volta para a tela "pronto" em vez de mostrar o
                        // resultado individual — só o agregado final é
                        // mostrado numa bateria (ver STATE_RANDOM_BATTERY_RESULT).
                        AdvanceToNextBatteryValue();
                        ProgramState = STATE_STARTING_TEST;
                    } else {
                        BatteryResultSelection = 0;
                        ProgramState = STATE_RANDOM_BATTERY_RESULT;
                    }
                } else {
                    memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
                    strcpy(lcd_line_buffer, "Reacao: ");

                    LongToStr(ReactionTimeDifference, conversions_buffer);
                    Ltrim(conversions_buffer);

                    strcat(lcd_line_buffer, conversions_buffer);
                    strcat(lcd_line_buffer, " ms");

                    Lcd_Out(1, 1, lcd_line_buffer);

                    // Mostra a janela [0, TestPeriodo) considerada "no alvo"
                    // e classifica o resultado.
                    memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
                    strcpy(lcd_line_buffer, "Alvo: 0 a ");
                    LongToStr(TestPeriodo, conversions_buffer);
                    Ltrim(conversions_buffer);
                    strcat(lcd_line_buffer, conversions_buffer);
                    strcat(lcd_line_buffer, " ms");
                    Lcd_Out(2, 1, lcd_line_buffer);

                    memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
                    if (ReactionTimeDifference < 0) {
                        strcpy(lcd_line_buffer, "Cedo");
                    } else if (ReactionTimeDifference < TestPeriodo) {
                        strcpy(lcd_line_buffer, "No alvo");
                    } else {
                        strcpy(lcd_line_buffer, "Atrasado");
                    }
                    Lcd_Out(3, 1, lcd_line_buffer);

                    ProgramState = STATE_FINISHED_TEST;
                }

                // Religa a recepção do UART1. Limpa um possível overrun
                // (RCSTA.OERR) primeiro: bytes podem ter se acumulado no
                // buffer de 2 bytes do hardware durante o teste (CREN nunca
                // foi desligado, só a interrupção RCIE) e travado a recepção
                // sem que a ISR estivesse ativa para perceber e corrigir.
                if (OERR_bit) {
                    CREN_bit = 0;
                    CREN_bit = 1;
                }
                RCIF_bit = 0;
                RCIE_bit = 1;

                UnpauseTimer0();
            break;
            case STATE_FINISHED_TEST:
            break;
            default:
                break;
        }

        Delay_ms(10);
    }
}
