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
#define TIMER0_LOAD_HIGH 0xEC
#define TIMER0_LOAD_LOW  0x89

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

// Limites atualizados para bater com o Aplicativo Android/Desktop (0 a 5000)
#define MIN_PERIODO 0
#define MAX_PERIODO 5000
#define PERIODO_STEP 5

i16 TestPeriodo = 100;

#define NUM_LEDS 32
u8 CurrentLed = 0;

// LED alvo fixo (numeração de 1 a NUM_LEDS)
#define TARGET_LED_NUMBER 25
#define TARGET_LED_INDEX  (TARGET_LED_NUMBER - 1)

// Variáveis para comunicação Bluetooth não-bloqueante
char bl_buffer[10];
u8 bl_idx = 0;
u8 bl_receiving = 0;

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
// ambiguidade com o payload numérico de bl_send_reaction_time(); ver
// Docs/dev/PROTOCOL.md.
void bl_send_period_ack(u8 applied) {
    UART1_Write('<');
    UART1_Write(applied ? 'A' : 'B');
    UART1_Write('>');
}

// Um teste em andamento já calculou TimeMeantForUserReaction a partir do
// TestPeriodo vigente; aceitar um novo valor nesse meio tempo desincroniza o
// cálculo do tempo de reação da velocidade real do chaser (ver KNOWN_ISSUES).
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

// Polling não bloqueante, verifica pacotes no buffer UART
void check_bluetooth() {
    while (UART1_Data_Ready()) {
        char byte = UART1_Read();

        if (byte == '<') {
            bl_receiving = 1;
            bl_idx = 0;
        } else if (byte == '>') {
            bl_receiving = 0;
            bl_buffer[bl_idx] = '\0';

            // Ignora o novo período durante um teste em andamento (ver
            // IsTestInProgress) para não desincronizar o resultado; o pacote
            // ainda é consumido normalmente para não travar o parser.
            if (!IsTestInProgress()) {
                // Converte o pacote string ASCII recebido pelo app em numérico
                TestPeriodo = atoi(bl_buffer);

                // Aplica os Clampings
                if (TestPeriodo < MIN_PERIODO) TestPeriodo = MIN_PERIODO;
                if (TestPeriodo > MAX_PERIODO) TestPeriodo = MAX_PERIODO;

                bl_send_period_ack(1);
            } else {
                bl_send_period_ack(0);
            }

        } else if (bl_receiving && bl_idx < 9) {
            bl_buffer[bl_idx++] = byte;
        }
    }
}

// ---------------- INTERRUPTS ----------------

void interrupt() {
    u16 current_timer;

    if(TMR0IF_bit) {
        TMR0IF_bit  = 0x00;
        ReloadTimer0();

        if(ProgramState == STATE_RUNNING_TEST) {
            LedExposition++;
            TimeSinceTestStarted++;

            if(LedExposition >= TestPeriodo){
                SET_LED_DMUX(CurrentLed);
                CurrentLed++;
                LedExposition = 0;

                if (CurrentLed >= NUM_LEDS) {
                    SET_LED_DMUX(0);
                    CurrentLed = 0;
                    current_timer = TimeSinceTestStarted;
                    ReactionTimeDifference = (i32)current_timer - (i32)TimeMeantForUserReaction;
                    ProgramState = STATE_CALCULATE_TEST_RESULT;
                }
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
    float voltage;
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
    voltage = ((float)(adc_value) * 5.0 / 4095.0) * 2.0;

    if (voltage >= 8.4) percent = 100;
    else if (voltage <= 6.0) percent = 0;
    else percent = (u8)((voltage - 6.0) * (100.0 / 2.4));

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

    Lcd_Out(1, 1, "Período: ");
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
    TRISD   = 0x00;                             
    TRISE.B2= 0x00;                             
    LATE.B2 = 0; 

    // RWM BUG FIX: Inicializa e limpa toda a LATC usando o registrador Latch.
    TRISC = 0x00;
    LATC = 0x00;

    ADCON1 = 0x0E;          
    ADCON2 = 0b10100101;    
    TRISA.B0 = 1;           

    Lcd_Init();
    Lcd_Cmd(_LCD_CLEAR);               
    Lcd_Cmd(_LCD_CURSOR_OFF);          

    UART1_Init(9600);
    Delay_ms(100);

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
                Lcd_Out(2, 1, "Aperte o botão de teste");
                Lcd_Out(3, 1, "para começar");

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
                strcpy(lcd_line_buffer, "Reação: ");

                LongToStr(ReactionTimeDifference, conversions_buffer);
                Ltrim(conversions_buffer);

                strcat(lcd_line_buffer, conversions_buffer);
                strcat(lcd_line_buffer, " ms");

                Lcd_Out(1, 1, lcd_line_buffer);
                ProgramState = STATE_FINISHED_TEST;

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
