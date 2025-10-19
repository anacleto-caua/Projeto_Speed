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


// *************************** INTERRUPÇÃO   ***************************
 void interrupt()
{
    // -- Trata Interrupção timer0 --
    if(TMR0IF_bit) // Houve interrupção no
    {                        
        TMR0IF_bit=0x00;
    }

    // -- Trata Interrupção Externa 0 --
    if(INT0IF_bit)                              //Houve interrupção externa 0?  => TESTE ENCERRADO - > BOTÃO TESTE PRESSIONADO
    {                                           //Sim...
        INT0IF_bit = 0x00;                      //limpa flag INT0IF
    }

    // -- Trata Interrupção Externa 1 --
    if(INT1IF_bit)                              //Houve interrupção externa 1?  =>encoder rotativo
    {                                           //Sim...
        INT1IF_bit = 0x00;                      //limpa flag INT1IF
    }

     // -- Trata Interrupção Externa 2 --
    if(INT2IF_bit)                              //Houve interrupção externa 2?  => SW do encoder
    {                                           //Sim...
       INT2IF_bit = 0x00;                       //limpa flag INT2IF
    }

}

void main() {
    // *************************** REGISTRADORESA ***************************
    CMCON = 0x07;                               // Desabilita os comparadores
    T0CON = 0x88;                               //configura timer0  16 bits
    TMR0H = 0xEC;
    TMR0L = 0x89;                               // inicia contagem em 60536   - base de tempo de 1 ms

    ADCON1  = 0x0F;                             //Configura os pinos do PORTB como digitais   (00001111b).  Desabilita entradas anal�gicas
    INTCON  = 0xB0;                             //Habilita interrupção global e interrupção externa 0   0x90

    // -- Registrador INTCON2 (pag 96 datasheet) --
    INTEDG0_bit = 0x00;                         //Configura interrupção externa 0 por borda de descida
    INTEDG1_bit = 0x00;                         //Configura interrupção externa 1 por borda de descida
    INTEDG2_bit = 0x01;                         //Configura interrupção externa 2 por borda de subida

    // -- Registrador INTCON3 (pag 97 datasheet) --
    INT1IE_bit  = 0x01;                         //Habilita interrupção externa 1
    INT2IE_bit  = 0x01;                         //Habilita interrupção externa 2

    TRISB   = 0xFF;                             // Configura os pinos do PORTB como entradas
    TRISD   = 0x00;                             // Configura os pinos do PORTD como saídas
    TRISC   = 0xE0;                             // configura C0 a C4 como saida
    TRISE.B2= 0x00;                             //configura E2 como saída (pino 10 para teste)
    PORTC   = 0x00;                             // inicia porta C em low

    while(1)
    {
        LATE.B2=0X01;
        delay_ms(200);
        LATE.B2=0X00;
        delay_ms(200);
    
    }

}