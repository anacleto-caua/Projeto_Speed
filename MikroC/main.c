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

//*********************** VARIAVEIS DE USO GLOBAL ***************************

// Possive�s entradas do encoder
typedef enum {
    ENCODER_NONE,
    ENCODER_UP,
    ENCODER_DOWN
} EncoderInput;
// Flag do input do encoder
volatile EncoderInput currentInput = ENCODER_NONE;

// Possive�s estados do programa
typedef enum {
    STATE_IDLE,                        // Dispositivo iniciado mas sem nada a fazer
    STATE_STARTING_TEST,               // Teste de reação iniciado
    STATE_RUNNING_TEST,                // Teste de reação sendo utilizado         
    STATE_RENDERING_MENU,              // O menu de configuração está rodando
    STATE_SELECTING_MENU,
    STATE_ERROR
} ProgramState;
// Flag de estado do programa
volatile ProgramState currentState = STATE_IDLE;

int flag_blink = 0;
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

//*********************** INTERRUPCAO   ***************************
 void interrupt()
{
    // -- Trata Interrupção timer0 --
    if(TMR0IF_bit)
    {
        TMR0IF_bit=0x00;
        // Recarrega o timer - TODO:conferir se realmente se faz necessário
        TMR0H = 0xEC;
        TMR0L = 0x89;
    }

    // -- Trata Interrupção Externa 0 -- Botão do teste
    if(INT0IF_bit)
    {
        INT0IF_bit = 0x00;
        blink();
    }

    // -- Trata Interrupção Externa 1 -- Clock do encoder
    if(INT1IF_bit)
    {
        INT1IF_bit = 0x00;
        blink();
        if(PORTB.B3 == 1) {
            currentInput = ENCODER_UP;
        } else {
            currentInput = ENCODER_DOWN;
        }
    }

     // -- Trata Interrupção Externa 2 -- Click do encoder
    if(INT2IF_bit)
    {
        INT2IF_bit = 0x00;
        blink();
        if(currentState == STATE_IDLE) {
            currentState = STATE_RENDERING_MENU;
        }
        else if (currentState == STATE_SELECTING_MENU) {
            blink();
            // Lógica para "selecionar" o item
            // ex: if (selected == 2) currentState = STATE_STARTING_TEST;
        }
    }
}

//*********************** OUTRAS FUNÇÕES ***************************
void initLcd()
{
    Lcd_Init();
    Lcd_Cmd(_LCD_CLEAR);               // Limpa o display
    Lcd_Cmd(_LCD_CURSOR_OFF);          // Desliga o cursor
}

void initRenderMenu()
{
    Lcd_Cmd(_LCD_CLEAR);
    // Lcd_Cmd(_LCD_BLINK_CURSOR_ON);      // Desliga o cursor
}

int selected = 0;
int op1 = 0;
int op2 = 1;
const int numMenuOptions = 3;
// Deveria ser uma constante mas causa erros
// Os espa�os s�o pra sobrescrever o buffer, e n�o deixar a �ltima letra da maior palavra ocupando espa�o. TODO: produzir uma solu��o mais sofisticada
char* menuItems[] = {
    "Período      ",
    "Display      ",
    "Iniciar      "
};


void renderMenu()
{
    switch (currentInput) {
        case ENCODER_UP:
        selected--;
        break;
        case ENCODER_DOWN:
        selected++;
        break;
        case ENCODER_NONE: // Não faz nada se não ouvir input no clock do encoder
        default:
            // Opção inexistente
        break;
    }
    currentInput = ENCODER_NONE; // Limpa a flag do input depois de usar
    
    // Creio que dê pra melhorar essa parte, diminuindo uma variávelou reoorganizando as condições
    op1 = selected;
    op2 = selected + 1;

    if(selected < 0)
    {
        selected = numMenuOptions - 1;
        op1 = selected;
        op2 = 0;
    }
    else if(selected >= numMenuOptions)
    {
        selected = 0;
        op1 = 0;
        op2 = 1;
    }
    else if(selected == numMenuOptions - 1)
    {
        selected = 2;
        op1 = 2;
        op2 = 0;
    }

    // Lcd_Cmd(_LCD_CLEAR); // Teoricamente não deveria limpar toda hora, mas é necessário reescrever o buffer nas posições que não contém um espaço vazio
    Lcd_Out(1, 1,  ">");
    Lcd_Out(1, 2, menuItems[op1]);

    Lcd_Out(2, 1, " ");
    Lcd_Out(2, 2, menuItems[op2]);
}

void main() {
        
    RCON.IPEN = 0;                              // Desabilita a prioridade de input, assim todas interrup��es rodam no interrupt() ignorando o interrupt_low() -- Conversar com professor --
    
    // *************************** REGISTRADORESA ***************************
    CMCON = 0x07;                               // Desabilita os comparadores
    T0CON = 0x88;                               //configura timer0  16 bits
    TMR0H = 0xEC;
    TMR0L = 0x89;                               // inicia contagem em 60536   - base de tempo de 1 ms

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

    // *************************** CORPO DO PROGRAMA ***************************
    LATE.B2 = 0; // Desliga o led de test

    initLcd();

    while(1) {
        // This is the core of the Finite State Machine
        switch(currentState) {

            case STATE_IDLE:
                Lcd_Out(1, 1, "Dispositivo");
                Lcd_Out(2, 1, "iniciado.");
                break;
            case STATE_RENDERING_MENU:
                initRenderMenu();
                currentState = STATE_SELECTING_MENU;
                break;
            case STATE_SELECTING_MENU:
                renderMenu();
                break;
            default:
                // Estado inexistente, programa deve ser encerrado.
                break;
        }

        // Roda o blink apenas para teste
        if(flag_blink){
            LATE.B2=0X01;
            // delay_ms(200);
            LATE.B2=0X00;
            
            flag_blink = 0;
        }
    }
}