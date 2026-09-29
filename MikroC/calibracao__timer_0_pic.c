/* *********************************************************
Cristal 20 MHz
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

// *************************** MAPEAMENTO DE HARDWARE
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

// *************************** DEFINIÇÃO DE CONSTANTES
 #define data_enc        PORTB.B3
 #define varaux          LATD6_bit
 #define mult_periodo    50        // valor para multiplicar a variável período e definir o periodo real

// *************************** DEFINIÇÃO DE VARÁVEIS
unsigned char encoder=0x00, flags=0x00, iter=0x00, menu=0x00, periodo=0x00, ciclo=0x00, disparo=0x00;
signed long milisec=0x00, milisecaux=0x00 , milisec_dis=0x00;
unsigned int  milisec_batt=0x00;
// ***************************   PROTÓTIPO DE FUNÇÇOES

//***************************    ROTINA DE INTERRUPÇÃO
 void interrupt()
{
    // -- Trata Interrupção timer0 --
    if(TMR0IF_bit)                           // Houve interrupção no Timer0
    {                                        //sim
        TMR0IF_bit=0x00;
        TMR0H = 0xEC;
        //TMR0L = 0x8C; 
        TMR0L = 0x8B;         
                            // inicia contagem em 61536   - base de tempo de 1 ms
        milisec++;
        LATB5_bit =~ LATB5_bit;
                              
         if(flags.B4)   //***************** TESTE INICIADO **************************
         {


            if(milisec>=periodo*mult_periodo*iter)
            {

             //  if(PORTC==disparo+1 || PORTC>31) PORTC=0x00;            //MÁXIMO 30 LEDS

               iter ++;
               PORTC++;
               if(PORTC>32)
              {
                          menu=0x06;
                          flags.B4=0x00;
              }
            }

         }
         else
         {
                  if(milisecaux%600==0) flags.B5=~flags.B5;
         
         }
          milisec_batt ++;
         //---------------------comentar - rotina para teste
        /* if(iter==0x08)  // parar o teste no LED especificado
         {
           varaux=0x00;
         } */
          //--------------------- comentar tambem varaux=0x01
                                 // na função iniciar
         milisecaux++;
         LATE.B2= ~LATE.B2;

    } //end if Timer0
    
    // -- Trata Interrupção Externa 0 --
     if(INT0IF_bit)                             //Houve interrupção externa 0?  => TESTE ENCERRADO - > BOTÃO TESTE PRESSIONADO
    {                                          //Sim...
        INT0IF_bit = 0x00;                     //limpa flag INT0IF

       if(menu==0x05 && flags.B4)  // O teste foi iniciado ?
       {                           // sim...
            flags.B4   = 0x00;                          // interrompe o teste
            menu       = 0x06;                          // mostra resultados
       }

       if(menu==0x04 && !flags.B4 && milisec_dis>0x00)  // O teste está preparado para iniciar ?
       {                           // sim...
        menu=0x05;                  //seta menu para teste iniciado
        milisec=0x00;
        iter=0x00;
        PORTC=0x00;
        flags.B4=0x01;              //Inicia o teste

       }

    } //end if INT0IF

    // -- Trata Interrupção Externa 1 --
    if(INT1IF_bit)                             //Houve interrupção externa 1?  =>encoder rotativo
    {                                          //Sim...
        INT1IF_bit = 0x00;                     //limpa flag INT1IF
        if(data_enc)    encoder++; else encoder--;
        if(encoder>254) encoder=0;
        if (encoder>250) encoder =250;


    //  LATE2_bit=~LATE2_bit;
       } //end if INT1IF

 // -- Trata Interrupção Externa 2 --
    if(INT2IF_bit)                             //Houve interrupção externa 2?  => SW do encoder
    {                                          //Sim...

        INT2IF_bit = 0x00;                     //limpa flag INT2IF
        if(flags.B1)return;                    //  rotina anti bousing
        flags.B1=0x01;                         //rotina anti bousing
        milisecaux=0x00;
        flags.B2=0x01;                         // indica SW pressionado
      //  LATE2_bit=~LATE2_bit;
    } //end if INT2IF

}//end interrupt

void main() {
     CMCON   = 0x07;
     T0CON = 0x88;                             //configura timer0  16 bits
     TMR0H = 0xEC;
     TMR0L = 0x89;                             // inicia contagem em 60536   - base de tempo de 1 ms

      ADCON1  = 0x0E;                           //Configura os pinos A0 analógico os demais digitais
     INTCON  = 0xB0;                            //Habilita interrupção global e interrupção externa 0   0x90

     // -- Registrador INTCON2 (pag 96 datasheet) --
     INTEDG0_bit = 0x00;                       //Configura interrupção externa 0 por borda de descida
     INTEDG1_bit = 0x00;                       //Configura interrupção externa 1 por borda de descida
     INTEDG2_bit = 0x01;                       //Configura interrupção externa 2 por borda de subida
     // --

     // -- Registrador INTCON3 (pag 97 datasheet) --
     INT1IE_bit  = 0x01;                       //Habilita interrupção externa 1
     INT2IE_bit  = 0x01;                       //Habilita interrupção externa 2
          // --
  
     TRISA   = 0xFF;                            //  como  entrada
     TRISB   = 0xEF;                           // Configura os pinos do PORTB como entradas
     TRISD   = 0x00;                          // Configura os pinos do PORTD como saídas
     TRISC   = 0xE0;                          // configura C0 a C4 como saida
     PORTC   = 0x00;                         // inicia porta C em low
     TRISE.B2=0x00;                          //configura E2 como saída (pino 10 para teste)
       TRISB.B5=0x00; 
    // IPEN_bit=0x01;                          //habilita prioridade de interrupção (RCON pág 44)

     Lcd_Init();                        // Initialize LCD
     Lcd_Cmd(_LCD_CLEAR);               // Clear display
     Lcd_Cmd(_LCD_CURSOR_OFF);          // Cursor off
      TRISE=0X00;
      PORTE.B2=0x00;
      
      

    //fboot();
    // --- Configurações iniciais
                   // PERIODO x mult_periodo (ms)
 
    if(periodo==0xFF)    
    {
         periodo=0x02;
   // seta perído=10 quando pic for reprogramado
      
    }

  //  periodo = 0x08;                              //------------C O M E N T A R
    if(disparo==0xFF)
    {
        disparo=0x0A; // seta disparo=18 quando pic for reprogramado
    }
    //disparo =0x0A;       // LED nr 20           //------------C O M E N T A R
    //ciclo   = 0x32;     //50%
    menu    = 0x00;
    encoder = 0x04;
   // TRISB.B4=0x00;

    while(1)                                  //Loop infinito
     {
           

     } // end while
}
