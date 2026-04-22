
/* *********************************************************
Cristal 20 MHz (5 MHz)
Ciclo de máquina 200nS
Base de tempo de 1 ms -> Contador do timer0 (16 bits -  0 a 65536) inicia em    60536
     TMR0H = 0xEC;
     TMR0L = 0x78; (0X89 empiricamente)
***********************************************************/

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

// Macro para gerar a string view em comptime
#define MAKE_VIEW(str) { str, sizeof(str) - 1 }

// Macros para converter constantes numéricas em texto no tempo de compilação
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

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
#define TIMER0_LOAD_HIGH 0xEC
#define TIMER0_LOAD_LOW  0x89

// Portas do enconder
#define ENCODER_SIGNAL_PORT PORTB.B3

// Porta do dmux do led
#define LED_DMUX_PORT PORTC

// Possiveis estados do programa       // u8
#define STATE_IDLE                  0  // Dispositivo iniciado mas sem nada a fazer
#define STATE_INIT_RENDER_MENU      1  // Inicia o menu
#define STATE_SELECTING_MENU        2  // Selecionando alguma opção no menu
#define STATE_INIT_CONFIG_PERIODO   3  // Inicia o menu de configuracao do periodo
#define STATE_CONFIG_PERIODO        4  // Configurando o período (tempo entre um led e outro piscar)
#define STATE_INIT_CONFIG_DISPLAY   5  // Inicia o menu de configuracao do display
#define STATE_CONFIG_DISPLAY        6  // Configurando o display (qual dos N leds vai piscar)
#define STATE_STARTING_TEST         7  // Prepara o teste, comunica o usuário para apertar o botão
#define STATE_TEST_READY            8  // Teste está pronto para começar, basta o usuário apertar o botão
#define STATE_TEST_BEGIN            9  // Teste de reação iniciado
#define STATE_RUNNING_TEST          10 // Teste de reação rodando
#define STATE_CALCULATE_TEST_RESULT 11 // Usuário finalizou o teste
#define STATE_FINISHED_TEST         12 // Usuário finalizou o teste
#define STATE_ERROR                 13 // Erro no sistema

// Flag de estado do programa
volatile u8 ProgramState = STATE_IDLE;

// Variáveis de teste
// Variáveis em millisegundos
#define MIN_PERIODO 50      // u16
#define MAX_PERIODO 1000    // u16
#define PERIODO_STEP 50     // u16

i16 TestPeriodo = 500;    // Quanto tempo de luz dar para cada led

// 32 LEDS ENUMERADOS DE 0 A 31
// Para o usuário será enumerado de 1 - 32 por simplicidade
#define NUM_LEDS 32     // u8
u8 CurrentLed = 0;      // Qual led está acesso
i8 TargetLed  = 0;      // Qual led vai ser o principal do teste

// Menu
// --- Item Período ---
void PeriodoOnClick() {
    switch (ProgramState) {
        // Indica que o usuário estava no menu de seleção
        case STATE_SELECTING_MENU:
            // Marca o estado como configurando o período
            ProgramState = STATE_INIT_CONFIG_PERIODO;
        break;
        // Indica que a configuração foi confirmada
        case STATE_CONFIG_PERIODO:
            // Volta a renderizar o menu
            ProgramState = STATE_INIT_RENDER_MENU;
        break;
        default:
        break;
    }
}

// --- Item Display ---
void DisplayOnClick() {
    switch (ProgramState) {
        // Indica que o usuário estava no menu de seleção
        case STATE_SELECTING_MENU:
            // Marca o estado como configurando o período
            ProgramState = STATE_INIT_CONFIG_DISPLAY;
        break;
        // Indica que a configuração foi confirmada
        case STATE_CONFIG_DISPLAY:
            // Volta a renderizar o menu
            ProgramState = STATE_INIT_RENDER_MENU;
        break;
        default:
        break;
    }
}

// --- Item Iniciar ---
void IniciarOnClick() {
    ProgramState = STATE_STARTING_TEST;
}

// Ponteiros para funções de cada estado do menu
typedef void (*OnClickFunc)(void);

// Struct para definir das possiveís opções do menu
typedef struct {
    StrView Name;
    OnClickFunc OnClick;
} MenuOption;

// Atualmente o menu não suporta mais opções que linhas no display lcd.
#define NUM_MENU_ITEMS 3
const MenuOption MenuItems[NUM_MENU_ITEMS] = {
    { MAKE_VIEW("Periodo"), PeriodoOnClick },
    { MAKE_VIEW("Display"), DisplayOnClick },
    { MAKE_VIEW("Iniciar"), IniciarOnClick }
};

// Variável que indica qual opção do menu está selecionada
i8 SelectedMenuOption = 0;

// Input do encoder
volatile i8 EncoderInput = 0;

i8 getEncoderInput() {
    i8 n = EncoderInput;
    EncoderInput = 0;
    return n;
}

// Definições do contador
u16 LedExposition = 0;                      // Conta o tempo de exposição de cada led
volatile u16 TimeSinceTestStarted = 0;      // Conta o tempo desde o começo do teste
volatile u16 TimeMeantForUserReaction = 0;  // Marca o tempo esperado da reação do usuário
volatile i32 ReactionTimeDifference = 0;    // Calcula a diferença do tempo esperado e do tempo que o usuário reagiu

void ReloadTimer0() {
    TMR0H = TIMER0_LOAD_HIGH;
    TMR0L = TIMER0_LOAD_LOW;
}

void PauseTimer0() {
    GIE_bit = 0;
}

void UnpauseTimer0() {
    GIE_bit = 1;
}

// Função pra ler a bateria manualmente
u16 Read_ADC_Manual() {
    ADCON0 = 0x01;
    Delay_us(20);
    GO_DONE_bit = 1;
    while (GO_DONE_bit == 1);
    return (((unsigned int)ADRESH << 8) | ADRESL);
}

// Funções do bluetooth
void bl_send_package(char* src, int package_size) {
    int i = 0;
    UART1_Write('<');

    for (i = 0; i < package_size; i++) {
        UART1_Write(*src);
        src++;
    }

    UART1_Write('>');
}

void bl_recv_package(char* dst, int package_size) {
    int began_receiving = 0;
    char byte;
    int received = 0;

    while(1) {
        if (UART1_Data_Ready() == 1) {
            byte = UART1_Read();
            switch (byte) {
                case '<':{
                        began_receiving = 1;
                    }
                    break;
                case '>':{
                        if (began_receiving == 1) {
                            if (received != package_size) {
                                // Invalid package, restart receiving
                                received = 0;
                                began_receiving = 0;
                            } else {
                                return;
                            }
                        }
                    }
                    break;
                default: {
                        *(dst+received) = byte;
                        received++;
                    }
                    break;
            }
        }
    }
}

// Interrupções
void interrupt() {
    u16 current_timer;

    // -- Trata Interrupção timer0 --
    if(TMR0IF_bit) {
        TMR0IF_bit  = 0x00;
        ReloadTimer0();

        // Mais um ciclo concluído, próximo led
        if(ProgramState == STATE_RUNNING_TEST) {
            LedExposition++;
            TimeSinceTestStarted++;

            if(LedExposition >= TestPeriodo){
                LED_DMUX_PORT++;    // Muda o LED
                LedExposition = 0;  // Reseta apenas o ritmo, mantendo o tempo total

                if (LED_DMUX_PORT >= NUM_LEDS) {
                    LED_DMUX_PORT = 0;
                    // Teste terminou sem reação do usuário, calcula o tempo de reação assim mesmo
                    current_timer = TimeSinceTestStarted;
                    ReactionTimeDifference = current_timer - TimeMeantForUserReaction;
                    ProgramState = STATE_CALCULATE_TEST_RESULT;
                }
            }
        }
    }

    // -- Trata Interrupção Externa 0 -- Botão do teste
    if(INT0IF_bit) {
        INT0IF_bit = 0x00;

        switch (ProgramState) {
            case STATE_TEST_READY:
                ProgramState = STATE_TEST_BEGIN;
                break;
            case STATE_RUNNING_TEST:
                // Usuário reagiu ao teste
                // Já calcula o tempo para que outra interrupção do timer0 não afete a contagem
                current_timer = TimeSinceTestStarted;
                ReactionTimeDifference = current_timer - TimeMeantForUserReaction;
                ProgramState = STATE_CALCULATE_TEST_RESULT;
                break;
        }
    }

    // -- Trata Interrupção Externa 1 -- Clock do encoder
    if(INT1IF_bit) {
        INT1IF_bit = 0x00;

        if(ENCODER_SIGNAL_PORT == 1) {
            EncoderInput++;
        } else {
            EncoderInput--;
        }
    }

     // -- Trata Interrupção Externa 2 -- Click do encoder
    if(INT2IF_bit)
    {
        INT2IF_bit = 0x00;

        switch(ProgramState) {
            // Abre o menu principal
            case STATE_FINISHED_TEST:
                ProgramState = STATE_INIT_RENDER_MENU;
            break;
            // Lógica de selecionar a opcao do menu
            case STATE_SELECTING_MENU:
            case STATE_CONFIG_DISPLAY:
            case STATE_CONFIG_PERIODO:
                MenuItems[SelectedMenuOption].OnClick();
            break;
        }
    }
}

// TODO: Considerar uma solução mais robusta e limpa ao invés dessa função para acessar a ROM
void strcpy_ROM_to_RAM(char* ram_dest, const char* rom_src) {
    char c;
    // Loop até encontrar o caractere nulo '\0'
    while (c = *rom_src++) {
        *ram_dest++ = c;
    }
    // Adiciona o caractere nulo no final do buffer da RAM
    *ram_dest = '\0';
}

void renderMenu() {
    // Buffer na RAM
    u8 i;
    char lcd_line_buffer[LCD_COLLUMN_COUNT];

    // Variavéis para testar a bateria
    float voltage;
    volatile u32 adc_value;
    u8 percent;

    SelectedMenuOption += getEncoderInput();

    // Clamping
    if(SelectedMenuOption < 0) {
        SelectedMenuOption = NUM_MENU_ITEMS - 1;
    }
    else if(SelectedMenuOption >= NUM_MENU_ITEMS) {
        SelectedMenuOption = 0;
    }

    // Draw menu
    for (i = 0; i < NUM_MENU_ITEMS; i++) {
        memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
        strcpy_ROM_to_RAM(lcd_line_buffer, MenuItems[i].Name.Data);
        Lcd_Out(i+1, 2, lcd_line_buffer);

        if (i == SelectedMenuOption) {
            Lcd_Out(i+1, 1, ">");
        } else {
            Lcd_Out(i+1, 1, " ");
        }
    }

    // Le o valor da bateria
    adc_value = Read_ADC_Manual();

    // Calcula Voltagem: (ADC * Vref / 4095) * Divid
    voltage = ((float)(adc_value) * 5.0 / 4095.0) * 2.0;

    // Calcula a porcentagem
    if (voltage >= 8.4) percent = 100;
    else if (voltage <= 6.0) percent = 0;
    else {
        percent = (u8)((voltage - 6.0) * (100.0 / 2.4));
    }

    // Mostra no lcd
    IntToStr(percent, lcd_line_buffer);
    Ltrim(lcd_line_buffer);
    Lcd_Out(4, (LCD_COLLUMN_COUNT - 3), lcd_line_buffer);
    Lcd_Out(4, LCD_COLLUMN_COUNT, "%");
}

void renderPeriodoMenu() {
    // Buffer de RAM para converter o número
    char periodo_buffer[7]; // Suficiente para "1000" e o nulo

    TestPeriodo += getEncoderInput() * PERIODO_STEP;

    if (TestPeriodo > MAX_PERIODO) TestPeriodo = MIN_PERIODO;
    if (TestPeriodo < MIN_PERIODO) TestPeriodo = MAX_PERIODO;

    IntToStr(TestPeriodo, periodo_buffer);

    Lcd_Out(1, 1, "Período: ");
    Lcd_Out(2, 1, periodo_buffer);
    Lcd_Out_Cp(" ms ");
}

void renderDisplayMenu() {
    // Variável temporária para desenhar no LCD
    u8 val;

    TargetLed += getEncoderInput();

    if (TargetLed >= NUM_LEDS) TargetLed = 0;
    if (TargetLed < 0) TargetLed = NUM_LEDS - 1;

    // O compilador vai transformar isso automaticamente em: Lcd_Out(1, 1, "Display: 1 - 32");
    Lcd_Out(1, 1, "Display: 1 - " STR(NUM_LEDS));

    // A linha 2 é dinâmica e pode ter um ou 2 digítos
    val = TargetLed + 1;

    if (val >= 10) {
        // Tem dois dígitos (Ex: 15)
        Lcd_Chr(2, 1, (val / 10) + '0'); // Extrai a dezena  (1)
        Lcd_Chr(2, 2, (val % 10) + '0'); // Extrai a unidade (5)
        Lcd_Out_Cp("-o ");               // O espaço no final limpa lixo antigo da tela
    } else {
        // Tem um dígito (Ex: 5)
        Lcd_Chr(2, 1, val + '0');        // Extrai a unidade (5)
        Lcd_Out_Cp("-o  ");              // Dois espaços extras para garantir a limpeza do LCD
    }
}

void main() {
    char lcd_line_buffer[LCD_COLLUMN_COUNT];    // Buffer para escrever na tela
    char conversions_buffer[10];                // Buffer temporário para conversões

    RCON.IPEN = 0;                              // Desabilita a prioridade de input, assim todas interrup��es rodam no interrupt() ignorando o interrupt_low() -- Conversar com professor --

    // *************************** REGISTRADORESA ***************************
    CMCON = 0x07;                               // Desabilita os comparadores
    T0CON = 0x88;                               //configura timer0  16 bits
    ReloadTimer0();

    ADCON1  = 0x0F;                             //Configura os pinos do PORTB como digitais   (00001111b).  Desabilita entradas anal�gicas
    INTCON  = 0xF0;                             //Habilita interrupção global e interrupção externa 0   0x90

    // -- Registrador INTCON2 (pag 96 datasheet) --
    INTEDG0_bit = 0x00;                         //Configura interrupção externa 0 por borda de descida
    INTEDG1_bit = 0x00;                         //Configura interrupção externa 1 por borda de descida
    INTEDG2_bit = 0x01;                         //Configura interrupção externa 2 por borda de subida
    RBPU_bit = 0;          // 0 = Habilita pull-ups do PORTB

    // -- Registrador INTCON3 (pag 97 datasheet) --
    INT0IE_bit  = 0x01;                         //Habilita interrupção externa 0
    INT1IE_bit  = 0x01;                         //Habilita interrupção externa 1
    INT2IE_bit  = 0x01;                         //Habilita interrupção externa 2

    TRISB   = 0xFF;                             // Configura os pinos do PORTB como entradas
    TRISD   = 0x00;                             // Configura os pinos do PORTD como saídas
    TRISC   = 0xE0;                             // configura C0 a C4 como saida
    TRISE.B2= 0x00;                             //configura E2 como saída (pino 10 para teste)
    PORTC   = 0x00;                             // inicia porta C em low

    LATE.B2 = 0; // Desliga o led de teste

    // Configura todas as portas C como saída - responsáveis pelo controle dos LEDs
    TRISC = 0x00;

    // Configura pinos da bateria
    ADCON1 = 0x0E;          // Cofigura AN0 como analógico
    ADCON2 = 0b10100101;    // Habilita leituras manuais
    TRISA.B0 = 1;           // Configura AN0 como entrada

    Lcd_Init();
    Lcd_Cmd(_LCD_CLEAR);               // Limpa o display
    Lcd_Cmd(_LCD_CURSOR_OFF);          // Desliga o cursor

    // Init bluetooth module
    UART1_Init(9600);
    Delay_ms(100);

    // *************************** CORPO DO PROGRAMA ***************************

    while(1) {
        // Máquina de estados
        switch(ProgramState) {
            // Dispositivo iniciado
            case STATE_IDLE:
                // Inutilizado atualmente
                ProgramState = STATE_INIT_RENDER_MENU;
            break;
            // Menu de seleção
            case STATE_INIT_RENDER_MENU:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_SELECTING_MENU;
            break;
            case STATE_SELECTING_MENU:
                renderMenu();
            break;
            // Menu de config - Período
            case STATE_INIT_CONFIG_PERIODO:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_CONFIG_PERIODO;
            break;
            case STATE_CONFIG_PERIODO:
                renderPeriodoMenu();
            break;
            //  Menu de config - Display
            case STATE_INIT_CONFIG_DISPLAY:
                Lcd_Cmd(_LCD_CLEAR);
                ProgramState = STATE_CONFIG_DISPLAY;
            break;
            case STATE_CONFIG_DISPLAY:
                renderDisplayMenu();
            break;
            // Test
            case STATE_STARTING_TEST:
                // Desliga a interrupção Timer0
                TMR0IE_bit = 0;

                Lcd_Cmd(_LCD_CLEAR);

                Lcd_Out(1, 1, "Teste pronto.");
                Lcd_Out(2, 1, "Aperte o botão de teste");
                Lcd_Out(3, 1, "para começar");

                // Pausa as interrupções para garantir que o tempo seja resetado com segurança
                LedExposition = 0;
                TimeSinceTestStarted = 0;
                TimeMeantForUserReaction = TargetLed * TestPeriodo;
                LED_DMUX_PORT = 0;
                // Reseta o contador novamente para que o primeiro ms seja contado por completo
                ReloadTimer0();

                ProgramState = STATE_TEST_READY;
            break;
            case STATE_TEST_READY:
                // ...
            break;
            case STATE_TEST_BEGIN:
                // Libera o interrupção Timer0
                TMR0IE_bit = 1;

                Lcd_Cmd(_LCD_CLEAR);
                Lcd_Out(1, 1, "Testando...");
                ProgramState = STATE_RUNNING_TEST;

            break;
            case STATE_RUNNING_TEST:
                //
            break;
            case STATE_CALCULATE_TEST_RESULT:
                // Desliga as interrupções do timer0 para evitar uma race condition
                PauseTimer0();

                Lcd_Cmd(_LCD_CLEAR);
                Delay_ms(5); // Garante que o tempo do teste foi calculado pela interrupção

                memset(&lcd_line_buffer, ' ', LCD_COLLUMN_COUNT);
                strcpy(lcd_line_buffer, "Reação: ");

                //reactionTimeDifference = 2909;
                IntToStr(ReactionTimeDifference, conversions_buffer);
                Ltrim(conversions_buffer);

                strcat(lcd_line_buffer, conversions_buffer);
                strcat(lcd_line_buffer, " ms");

                Lcd_Out(1, 1, lcd_line_buffer);
                ProgramState = STATE_FINISHED_TEST;

                UnpauseTimer0();
            break;
            case STATE_FINISHED_TEST:
                //
            break;
            default:
                // Estado inexistente, não pode ser alcançado.
                break;
        }

        // Garante maior estabilidade do programa
        Delay_ms(10);
    }
}
