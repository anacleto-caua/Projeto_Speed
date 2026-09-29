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
#define STATE_IDLE                  0
#define STATE_INIT_RENDER_MENU      1
#define STATE_SELECTING_MENU        2
#define STATE_INIT_CONFIG_PERIODO   3
#define STATE_CONFIG_PERIODO        4
#define STATE_INIT_CONFIG_BLUETOOTH 5
#define STATE_CONFIG_BLUETOOTH      6
#define STATE_STARTING_TEST         7
#define STATE_TEST_READY            8
#define STATE_TEST_BEGIN            9
#define STATE_RUNNING_TEST          10
#define STATE_CALCULATE_TEST_RESULT 11
#define STATE_FINISHED_TEST         12
#define STATE_ERROR                 13

volatile u8 ProgramState = STATE_IDLE;

// Limites atualizados para bater com o Aplicativo Android/Desktop (1 a 5000).
// MIN_PERIODO=0 permitia um chase degenerado de 0 ms: com TestPeriodo=0 o
// Timer0 avançava os 32 LEDs em ~32 ms e o "acerto" virava "aperte
// imediatamente ao iniciar o teste".
#define MIN_PERIODO 1
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

// Menu OnClicks
void PeriodoOnClick() {
    switch (ProgramState) {
        case STATE_SELECTING_MENU: ProgramState = STATE_INIT_CONFIG_PERIODO; break;
        case STATE_CONFIG_PERIODO: ProgramState = STATE_INIT_RENDER_MENU; break;
        default: break;
    }
}

void BluetoothOnClick() {
    switch (ProgramState) {
        case STATE_SELECTING_MENU: ProgramState = STATE_INIT_CONFIG_BLUETOOTH; break;
        case STATE_CONFIG_BLUETOOTH: ProgramState = STATE_INIT_RENDER_MENU; break;
        default: break;
    }
}

void IniciarOnClick() {
    ProgramState = STATE_STARTING_TEST;
}

typedef void (*OnClickFunc)(void);
typedef struct { StrView Name; OnClickFunc OnClick; } MenuOption;

#define NUM_MENU_ITEMS 3
const MenuOption MenuItems[NUM_MENU_ITEMS] = {
    { MAKE_VIEW("Periodo"),   PeriodoOnClick },
    { MAKE_VIEW("Bluetooth"), BluetoothOnClick },
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

#ifdef TIMER0_CALIBRATION_TEST
        LATB5_bit = ~LATB5_bit;
#endif

        if(ProgramState == STATE_RUNNING_TEST) {
            LedExposition++;
            TimeSinceTestStarted++;

            if(LedExposition >= TestPeriodo){
                LedExposition = 0;
                CurrentLed++;

                if (CurrentLed >= NUM_LEDS) {
                    SET_LED_DMUX(0);
                    CurrentLed = 0;
                    current_timer = TimeSinceTestStarted;
                    ReactionTimeDifference = (i32)current_timer - (i32)TimeMeantForUserReaction;
                    ProgramState = STATE_CALCULATE_TEST_RESULT;
                } else {
                    SET_LED_DMUX(CurrentLed);
                }
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
            case STATE_CONFIG_BLUETOOTH:
            case STATE_CONFIG_PERIODO:
                MenuItems[SelectedMenuOption].OnClick();
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

void renderMenu() {
    u8 i;
    char lcd_line_buffer[LCD_COLLUMN_COUNT];
    float raw_percent;
    i16 rounded_percent;
    volatile u32 adc_value;
    u8 percent;

    SelectedMenuOption += getEncoderInput();

    if(SelectedMenuOption < 0) SelectedMenuOption = NUM_MENU_ITEMS - 1;
    else if(SelectedMenuOption >= NUM_MENU_ITEMS) SelectedMenuOption = 0;

    for (i = 0; i < NUM_MENU_ITEMS; i++) {
        memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
        strcpy_ROM_to_RAM(lcd_line_buffer, MenuItems[i].Name.Data);
        Lcd_Out(i+1, 2, lcd_line_buffer);

        if (i == SelectedMenuOption) Lcd_Out(i+1, 1, ">");
        else Lcd_Out(i+1, 1, " ");
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

void renderPeriodoMenu() {
    char periodo_buffer[7]; 
    TestPeriodo += getEncoderInput() * PERIODO_STEP;

    if (TestPeriodo > MAX_PERIODO) TestPeriodo = MIN_PERIODO;
    if (TestPeriodo < MIN_PERIODO) TestPeriodo = MAX_PERIODO;

    IntToStr(TestPeriodo, periodo_buffer);

    Lcd_Out(1, 1, "Periodo: ");
    Lcd_Out(2, 1, periodo_buffer);
    Lcd_Out_CP(" ms ");
}

void renderBluetoothMenu() {
    if (getEncoderInput() != 0) {
        BluetoothEnabled = !BluetoothEnabled;
        ApplyBluetoothState();
    }

    Lcd_Out(1, 1, "Bluetooth:");
    if (BluetoothEnabled) Lcd_Out(2, 1, "Ligado    ");
    else Lcd_Out(2, 1, "Desligado ");
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

    while(1) {
        // Escuta constantemente atualizações Bluetooth do Android/Desktop
        check_bluetooth();

        switch(ProgramState) {
            case STATE_IDLE:
                ProgramState = STATE_INIT_RENDER_MENU;
            break;
            case STATE_INIT_RENDER_MENU:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_SELECTING_MENU;
            break;
            case STATE_SELECTING_MENU:
                renderMenu();
            break;
            case STATE_INIT_CONFIG_PERIODO:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_CONFIG_PERIODO;
            break;
            case STATE_CONFIG_PERIODO:
                renderPeriodoMenu();
            break;
            case STATE_INIT_CONFIG_BLUETOOTH:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_CONFIG_BLUETOOTH;
            break;
            case STATE_CONFIG_BLUETOOTH:
                renderBluetoothMenu();
            break;
            case STATE_STARTING_TEST:
                TMR0IE_bit = 0;
                Lcd_Cmd(_LCD_CLEAR);

                Lcd_Out(1, 1, "Teste pronto.");
                Lcd_Out(2, 1, "Aperte o botao de teste");
                Lcd_Out(3, 1, "para comecar");

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

                // Envia o Resultado para o App via Bluetooth
                bl_send_reaction_time(ReactionTimeDifference);

                Lcd_Cmd(_LCD_CLEAR);
                Delay_ms(5); 

                memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
                strcpy(lcd_line_buffer, "Reacao: ");

                LongToStr(ReactionTimeDifference, conversions_buffer);
                Ltrim(conversions_buffer);

                strcat(lcd_line_buffer, conversions_buffer);
                strcat(lcd_line_buffer, " ms");

                Lcd_Out(1, 1, lcd_line_buffer);

                // Mostra a janela [0, TestPeriodo) considerada "no alvo" e
                // classifica o resultado.
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
