        /* *********************************************************
Cristal 20 MHz (5 MHz)
Ciclo de máquina 200nS
Base de tempo de 1 ms -> Contador do timer0 (16 bits -  0 a 65536) inicia em    60536
     TMR0H = 0xEC;
     TMR0L = 0x78; (0X89 empiricamente)

-------------------flags
                        B0 -> mostrar mensagem quando o teste iniciar e quando exibir resultados
                        B1 -> habilita rotina anti bousing sw encoder
                        B3 -> SW pressionado
                        B4 -> inicia teste
                        b5 -> teste piscaled
***********************************************************/

// *************************** MAPEAMENTO DE HARDWARE   ***************************

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

//*********************** FUNÇÃO DE TESTE ***************************
volatile int flag_blink = 0;
// Função apenas para teste
void blink()
{
    if(flag_blink){
        flag_blink = 0;
    }
    else
    {
        flag_blink = 1;
    }
}

//*********************** VARIAVEIS DE USO GLOBAL ***************************
// Possive�s estados do programa
typedef enum {
    STATE_IDLE,                         // Dispositivo iniciado mas sem nada a fazer
    STATE_INIT_RENDER_MENU,             // Inicia o menu
    STATE_SELECTING_MENU,               // Selecionando alguma opção no menu
    STATE_INIT_CONFIG_PERIODO,          // Inicia o menu de configuracao do periodo
    STATE_CONFIG_PERIODO,               // Configurando o período (tempo entre um led e outro piscar)
    STATE_INIT_CONFIG_DISPLAY,          // Inicia o menu de configuracao do display
    STATE_CONFIG_DISPLAY,               // Configurando o display (qual dos N leds vai piscar)
    STATE_STARTING_TEST,                // Teste de reação iniciado
    STATE_TEST_READY,                   // Teste está pronto para começar, basta o usuário apertar o botão (ainda não implementado)
    STATE_RUNNING_TEST,                 // Teste de reação rodando
    STATE_CALCULATE_TEST_RESULT,        // Usuário finalizou o teste
    STATE_FINISHED_TEST,                // Usuário finalizou o teste
    STATE_ERROR
} ProgramState;

// Flag de estado do programa
volatile ProgramState currentState = STATE_IDLE;

// Possive�s entradas do encoder
typedef enum {
    ENCODER_NONE,
    ENCODER_UP,
    ENCODER_DOWN
} EncoderInput;

// Flag do input do encoder - Nunca acesse essa variável, sempre use a funcao getEncoderInput()
volatile EncoderInput _currentInput = ENCODER_NONE;

//*********************** VARIÁVEIS DE CONFUGURAÇÃO DO TESTE ***************************
// Variáveis em millisegundos
// Ambos _testPeriodo e _testDisplay não possuem unsigned pois isso quebra a lógica de wrap-arround
const unsigned int _minPeriodo = 50;
const unsigned int _maxPeriodo = 1000;
const unsigned int _periodoStep = 50;   // De quanto em quanto sobe no menu
int _testPeriodo = 100;                      // Quanto tempo de luz dar para cada led

// 32 LEDS ENUMERADOS DE 0 A 31
// Para o usuário será enumerado de 1 - 32 por simplicidade
const unsigned int _numLeds = 32;
unsigned long ledToBlink = 0;           // Qual led vai ser o próximo piscado
int _testDisplay = 0;                       // Qual led vai ser o principal do teste

//*********************** FUNÇÕES PARA RECEBER O INPUT DO MENU ***************************
// TODO: Considerar levar essa função para outro arquivo, por organização
// Tem de ser declaradas antes de serem utilizadas pelo menuItems

// --- Item Período ---
void periodo_onClick() {
    switch (currentState) {
        // Indica que o usuário estava no menu de seleção
        case STATE_SELECTING_MENU:
            // Marca o estado como configurando o período
            currentState = STATE_INIT_CONFIG_PERIODO;
        break;
        // Indica que a configuração foi confirmada
        case STATE_CONFIG_PERIODO:
            // Volta a renderizar o menu
            currentState = STATE_INIT_RENDER_MENU;
        break;
        default:
        break;
    }
}

// --- Item Display ---
void display_onClick() {
    switch (currentState) {
        // Indica que o usuário estava no menu de seleção
        case STATE_SELECTING_MENU:
            // Marca o estado como configurando o período
            currentState = STATE_INIT_CONFIG_DISPLAY;
        break;
        // Indica que a configuração foi confirmada
        case STATE_CONFIG_DISPLAY:
            // Volta a renderizar o menu
            currentState = STATE_INIT_RENDER_MENU;
        break;
        default:
        break;
    }
}

// --- Item Iniciar ---
void iniciar_onClick() {
    currentState = STATE_STARTING_TEST;
}

//*********************** OUTRAS VARIÁVEIS LIGADAS AO MENU ***************************
// Ponteiros para funções de cada estado do menu
// 'onClickFunc' é um ponteiro para uma função sem parâmetros que retorna void.
typedef void (*onClickFunc)(void);

// Struct para definir das possiveís opções do menu
typedef struct {
    char name[17];
    onClickFunc onClick;
} MenuOption;

// Variável que indica qual opção do menu está selecionada
int selected = 0;
// Número de opções do menu
const int numMenuItems = 3;
// Array com as opções do menu
const MenuOption menuItems[3] = {
    // Os espa�os no nome s�o pra sobrescrever o buffer, e n�o deixar a �ltima letra da maior palavra ocupando espa�o. TODO: produzir uma solu��o mais sofisticada
    { "Período     ", periodo_onClick },
    { "Display     ", display_onClick },
    { "Iniciar     ", iniciar_onClick }
};

// *************************** DEFINIÇÕES DO CONTADOR DE TEMPO ***************************

volatile int ledTimerCount = 0;             // Conta o tempo de exposição de cada led
volatile unsigned int timeSinceTestStarted = 0;      // Conta o tempo desde o começo do teste
volatile int timeMeantForUserReaction = 0;  // Marca o tempo esperado da reação do usuário
volatile int reactionTimeDifference = 0;    // Calcula a diferença do tempo esperado e do tempo que o usuário reagiu

// inicia contagem em 60536   - base de tempo de 1 ms
#define TMR0_LOAD_HIGH  0xEC
#define TMR0_LOAD_LOW   0x89

// Função pra ler a bateria manualmente
unsigned int Read_ADC_Manual() {
    ADCON0 = 0x01;
    Delay_us(20);
    GO_DONE_bit = 1;
    while (GO_DONE_bit == 1);
    return (((unsigned int)ADRESH << 8) | ADRESL);
}

void ReloadTimer0() {
    TMR0H = TMR0_LOAD_HIGH;
    TMR0L = TMR0_LOAD_LOW;
}

void PauseTimer0() {
    GIE_bit = 0;
}

void UnpauseTimer0() {
    GIE_bit = 1;
}
//*********************** INTERRUPCAO   ***************************
void interrupt()
{
    int currentTimer;

    // -- Trata Interrupção timer0 --
    if(TMR0IF_bit)
    {
        TMR0IF_bit  = 0x00;
        ReloadTimer0();

        timeSinceTestStarted++;
        // Mais um ciclo concluído, próximo led
        if(currentState == STATE_RUNNING_TEST) {
            timeSinceTestStarted++; // Conta o tempo total do teste
            ledTimerCount++;        // Conta o tempo para mudar o LED

            if(ledTimerCount >= _testPeriodo){
                PORTC++;           // Muda o LED
                ledTimerCount = 0; // Reseta apenas o ritmo, mantendo o tempo total

                if (PORTC >= _numLeds) {
                    PORTC = 0;  // Teste terminou sem reação do usuário, resetar o contador? Testar de novo começando do primeiro led? 
                }
            }
        }
    }

    // -- Trata Interrupção Externa 0 -- Botão do teste
    if(INT0IF_bit)
    {
        INT0IF_bit = 0x00;

        switch (currentState) {
            case STATE_TEST_READY:
                // Usuário apertou o botão de teste
                break;
            case STATE_RUNNING_TEST:
                // Usuário reagiu ao teste
                // Já calcula o tempo para que outra interrupção do timer0 não afete a contagem
                currentTimer = timeSinceTestStarted;
                reactionTimeDifference = currentTimer - timeMeantForUserReaction;
                currentState = STATE_CALCULATE_TEST_RESULT;
                break;
        }
    }

    // -- Trata Interrupção Externa 1 -- Clock do encoder
    if(INT1IF_bit)
    {
        INT1IF_bit = 0x00;

        if(PORTB.B3 == 1) {
            _currentInput = ENCODER_UP;
        } else {
            _currentInput = ENCODER_DOWN;
        }
    }

     // -- Trata Interrupção Externa 2 -- Click do encoder
    if(INT2IF_bit)
    {
        INT2IF_bit = 0x00;

        switch(currentState) {
            // Abre o menu principal
            case STATE_IDLE:
            case STATE_FINISHED_TEST:
                currentState = STATE_INIT_RENDER_MENU;
            break;
            // Lógica de selecionar a opcao do menu
            case STATE_SELECTING_MENU:
            case STATE_CONFIG_DISPLAY:
            case STATE_CONFIG_PERIODO:
                menuItems[selected].onClick();
            break;
        }
    }
}

//*********************** OUTRAS FUNÇÕES ***************************

// Função para garantir que sempre que a entrada do encoder for lida ela seja resetada
EncoderInput getEncoderInput()
{
    EncoderInput oldInput = _currentInput;
    _currentInput = ENCODER_NONE; // Reseta a entrada do encoder
    return  oldInput;
}

// TODO: Considerar uma solução mais robusta e limpa ao invés dessa função para acessar a ROM
void strcpy_ROM_to_RAM(char* ram_dest, const char* rom_src)
{
    char c;
    // Loop até encontrar o caractere nulo '\0'
    while (c = *rom_src++) {
        *ram_dest++ = c;
    }
    // Adiciona o caractere nulo no final do buffer da RAM
    *ram_dest = '\0';
}

void initLcd()
{
    Lcd_Init();
    Lcd_Cmd(_LCD_CLEAR);               // Limpa o display
    Lcd_Cmd(_LCD_CURSOR_OFF);          // Desliga o cursor
}

void clearLcd()
{
    Lcd_Cmd(_LCD_CLEAR);
}

void renderMenu()
{
    int op1;
    int op2;
    // Buffers na RAM
    char linha1_buffer[17];
    char linha2_buffer[17];

    switch (getEncoderInput()) {
        case ENCODER_UP:
            selected--;
            break;
        case ENCODER_DOWN:
            selected++;
            break;
        default:
            break;
    }

    // Para que as opções sejam válidas
    op1 = selected;
    op2 = selected + 1;

    if(selected < 0)
    {
        selected = numMenuItems - 1;
        op1 = selected;
        op2 = 0;
    }
    else if(selected >= numMenuItems)
    {
        selected = 0;
        op1 = 0;
        op2 = 1;
    }
    else if(selected == numMenuItems - 1)
    {
        selected = numMenuItems - 1;
        op1 = numMenuItems - 1;
        op2 = 0;
    }

    // Copia da ROM para a RAM usando NOSSA função
    strcpy_ROM_to_RAM(linha1_buffer, menuItems[op1].name);
    strcpy_ROM_to_RAM(linha2_buffer, menuItems[op2].name);

    Lcd_Out(1, 1, ">");
    Lcd_Out(1, 2, linha1_buffer); // Passa o buffer da RAM

    Lcd_Out(2, 1, " ");
    Lcd_Out(2, 2, linha2_buffer); // Passa o buffer da RAM
}

void renderPeriodoMenu()
{
    // O valor que há de ser registrado está na variável global _testPeriodo

    // Buffer de RAM para converter o número
    char periodoBuffer[7]; // Suficiente para "1000" e o nulo

    switch (getEncoderInput()) {
        case ENCODER_UP:
            _testPeriodo += _periodoStep;
        break;
        case ENCODER_DOWN:
            _testPeriodo -= _periodoStep;
            break;
    }

    if (_testPeriodo > _maxPeriodo) _testPeriodo = _minPeriodo;
    if (_testPeriodo < _minPeriodo) _testPeriodo = _maxPeriodo;

    IntToStr(_testPeriodo, periodoBuffer);

    Lcd_Out(1, 1, "Período: ");
    Lcd_Out(2, 1, periodoBuffer);
    Lcd_Out_Cp(" ms ");
}

void renderDisplayMenu()
{
    const int displayMin = 0;
    const int displayMax = _numLeds - 1;
    // O valor que há de ser registrado está na variável global _testDisplay

    // Buffers para saída de dados
    char bufferTemp[7];         // Buffer para conversões
    char bufferDisplayMsg[20];  // Suficiente para toda a extensão do display

    switch (getEncoderInput()) {
        case ENCODER_UP:
            _testDisplay++;
            break;
        case ENCODER_DOWN:
            _testDisplay--;
            break;
    }

    if (_testDisplay > displayMax)  _testDisplay = displayMin;
    if (_testDisplay < displayMin)  _testDisplay = displayMax;

    // Formatando o buffer: "Display: 1 - 32"
    strcpy(bufferDisplayMsg, "Display: ");
    IntToStr(displayMin + 1, bufferTemp);
    Ltrim(bufferTemp);
    strcat(bufferDisplayMsg, bufferTemp);
    strcat(bufferDisplayMsg, " - ");
    IntToStr(displayMax + 1, bufferTemp);
    Ltrim(bufferTemp);
    strcat(bufferDisplayMsg, bufferTemp);
    // Enviando ao Lcd
    Lcd_Out(1, 1, bufferDisplayMsg);

    // Formatando o buffer: "N-o" & Enviando ao LCD
    // Soma 1 para que o usuário veja de 1 - N e não 0 - N-1
    IntToStr(_testDisplay + 1, bufferTemp);
    Lcd_Out(2, 1, bufferTemp);
    Lcd_Out_Cp("-o");
    Lcd_Out_Cp(" ");                        // Caso troque de um número com 2 dígitos para 1 digíto sobrescreve artefatos
}

void main()
{
    char bufferTemp[16]; // Buffer temporário para conversões
    // Variavéis para testar a bateria
    float voltage;
    volatile unsigned int adc_value;
    unsigned int percent;
    char txt[15];

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
    INT0IE_bit  = 0x00;                         //Habilita interrupção externa 0
    INT1IE_bit  = 0x01;                         //Habilita interrupção externa 1
    INT2IE_bit  = 0x01;                         //Habilita interrupção externa 2

    TRISB   = 0xFF;                             // Configura os pinos do PORTB como entradas
    TRISD   = 0x00;                             // Configura os pinos do PORTD como saídas
    TRISC   = 0xE0;                             // configura C0 a C4 como saida
    TRISE.B2= 0x00;                             //configura E2 como saída (pino 10 para teste)
    PORTC   = 0x00;                             // inicia porta C em low

    LATE.B2 = 0; // Desliga o led de teste

    // ----------------- Configura todas as portas C como saída - responsáveis pelo controle dos LEDs
    TRISC = 0x00;

    // ---------------- Configura pinos da bateria

    ADCON1 = 0x0E;          // Cofigura AN0 como analógico
    ADCON2 = 0b10100101;    // Habilita leituras manuais
    TRISA.B0 = 1;           // Configura AN0 como entrada

    initLcd();

    // Teste para medir bateria, quebra o funcionamento normal do programa
    while(1) {
        // Liga o circuito de medida
        Delay_ms(20);

        // Le o valor
        adc_value = Read_ADC_Manual();

        // Desliga o circuito de medida -- economizar bateria

        // Calcula Voltagem: (ADC * Vref / 4095) * Divid
        voltage = ((float)(adc_value) * 5.0 / 4095.0) * 2.0;

        // Calcula a porcentagem
        if (voltage >= 8.4) percent = 100;
        else if (voltage <= 6.0) percent = 0;
        else {
            percent = (unsigned int)((voltage - 6.0) * (100.0 / 2.4));
        }

        // Mostra no lcd
        Lcd_Cmd(_LCD_CLEAR);
        Lcd_Out(1, 1, "Bat Voltage:");
        FloatToStr(voltage, txt);
        Lcd_Out(2, 1, txt);
        Lcd_Out(2, 7, "V");

        IntToStr(percent, txt);
        Ltrim(txt);
        Lcd_Out(2, 10, txt);
        Lcd_Out(2, 16, "%");

        // Delay de 30 segundos como recomendado
        Delay_ms(30000);
    }

    // *************************** CORPO DO PROGRAMA ***************************

    while(1) {
        // Máquina de estados
        switch(currentState) {
            // Dispositivo iniciado
            case STATE_IDLE:
                Lcd_Out(1, 1, "Dispositivo");
                Lcd_Out(2, 1, "iniciado...");
            break;
            // Menu de seleção
            case STATE_INIT_RENDER_MENU:
                clearLcd();
                currentState = STATE_SELECTING_MENU;
            break;
            case STATE_SELECTING_MENU:
                renderMenu();
            break;
            // Menu de config - Período
            case STATE_INIT_CONFIG_PERIODO:
                clearLcd();
                currentState = STATE_CONFIG_PERIODO;
            break;
            case STATE_CONFIG_PERIODO:
                renderPeriodoMenu();
            break;
            //  Menu de config - Display
            case STATE_INIT_CONFIG_DISPLAY:
                clearLcd();
                currentState = STATE_CONFIG_DISPLAY;
            break;
            case STATE_CONFIG_DISPLAY:
                renderDisplayMenu();
            break;
            // Test
            case STATE_STARTING_TEST:
                // Garante a interrupção do timer0 está disponível
                TMR0IE_bit = 1;

                clearLcd();
                Lcd_Out(1, 1, "Testando!");
                // Pausa as interrupções para garantir que o tempo seja resetado com segurança
                PauseTimer0();
                ledTimerCount = 0;
                timeSinceTestStarted = 0;
                timeMeantForUserReaction = _testDisplay * _testPeriodo;
                PORTC = 0;
                // Reseta o contador novamente para que o primeiro ms seja contado por completo
                ReloadTimer0();
                currentState = STATE_RUNNING_TEST;
                // Despausa
                UnpauseTimer0();
            break;
            case STATE_TEST_READY:
                //
            case STATE_RUNNING_TEST:
                //
            break;
            case STATE_CALCULATE_TEST_RESULT:
                // Desliga as interrupções do timer0 para evitar uma race condition
                PauseTimer0();

                clearLcd();
                Delay_ms(5);
                IntToStr(reactionTimeDifference, bufferTemp);
                Lcd_Out(1, 1, "Tempo: ");
                Lcd_Out(2, 1, bufferTemp);
                Lcd_Out_Cp(" ms");

                currentState = STATE_FINISHED_TEST;

                UnpauseTimer0();
            break;
            case STATE_FINISHED_TEST:
                //
            break;
            default:
                // Estado inexistente, não pode ser alcançado.
                break;
        }

        // Roda o blink apenas para teste
        if(flag_blink){
            LATE.B2=0X01;
            // delay_ms(200);
            LATE.B2=0X00;

            flag_blink = 0;
        }

        // Garante maior estabilidade do programa
        Delay_ms(10);
    }
}
