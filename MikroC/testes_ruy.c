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
void mostra_lcd(signed long valor, unsigned char lin , unsigned char col);
void configuracoes();
void fboot();
void menu_0();
void conf_periodo();
void conf_ciclo();
void conf_disparo();
void iniciar();
void pisca_lcd(char *msg,unsigned char lin , unsigned char col);
void ver_batt();
 
//***************************    ROTINA DE INTERRUPÇÃO
 void interrupt()
{
    // -- Trata Interrupção timer0 --
    if(TMR0IF_bit)                           // Houve interrupção no Timer0
    {                                        //sim
        TMR0IF_bit=0x00;
        TMR0H = 0xEC;
        TMR0L = 0x8C;                             // inicia contagem em 61536   - base de tempo de 1 ms

         if(flags.B4)   //***************** TESTE INICIADO **************************
         {
            milisec++;

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

    // IPEN_bit=0x01;                          //habilita prioridade de interrupção (RCON pág 44)

     Lcd_Init();                        // Initialize LCD
     delay_ms(100);
     Lcd_Cmd(_LCD_CLEAR);               // Clear display
     Lcd_Cmd(_LCD_CURSOR_OFF);          // Cursor off
      TRISE=0X00;
      PORTE.B2=0x00;
      
      
      ADC_Init();
      delay_ms(100);

    //fboot();
    // --- Configurações iniciais
    periodo = EEPROM_read(0x00);                     // PERIODO x mult_periodo (ms)
    delay_ms(10);
    if(periodo==0xFF)    
    {
         periodo=0x02;
         EEPROM_write(0x00,periodo);  // seta perído=10 quando pic for reprogramado
         delay_ms(10);
    }

  //  periodo = 0x08;                              //------------C O M E N T A R
    disparo = EEPROM_read(0x01);
    delay_ms(10);
    if(disparo==0xFF)
    {
        disparo=0x0A;
        EEPROM_write(0x01,disparo); // seta disparo=18 quando pic for reprogramado
        delay_ms(10);
    }
    //disparo =0x0A;       // LED nr 20           //------------C O M E N T A R
    //ciclo   = 0x32;     //50%
    menu    = 0x00;
    encoder = 0x04;
   // TRISB.B4=0x00;

    while(1)                                  //Loop infinito
     {
            if (milisec_batt>500) ver_batt(); 
            continue;

          if(menu==0) menu_0();
          else if (menu==1) conf_periodo(); // configura período
          else if (menu==2) conf_ciclo(); // configura ciclo ativo
          else if (menu==3) conf_disparo(); // configura qual led deve acender na hora do atleta apertar o botão
          else if (menu==4) iniciar(); // Preparado para iniciar
          else if (menu==5) //teste iniciado
          {

              if(!flags.B0)
              {
                 flags.B0=0x01;
                 Lcd_Cmd(_LCD_CLEAR);
                 lcd_Out (2,1,"TESTE INICIADO..");
              }


          }
          else if(menu==6)  //mostra resultados
          {
          

                if(flags.B0)
                {
                   flags.B0=0x00;
                   Lcd_Cmd(_LCD_CLEAR);
                   lcd_out(2,1,"Resultado:");
                   mostra_lcd((milisec - milisec_dis + periodo*mult_periodo),3,1);
                   lcd_out_cp(" ms");

                   milisec_dis=0x00;
                }
                // lcd_out(2,15,">>");
                pisca_lcd(">>",3,15);
                  if(flags.B2) // pressionou SW
                  {
                   flags.B2=0x00;
                   menu=0X04;
                   encoder=0x04;
                  }
          
          
          }
         if(menu!=5) ver_batt();
         configuracoes();

     } // end while
}

//*************************************************DECLARAÇÃO DE FUNÇÕES
void mostra_lcd(signed long valor, unsigned char lin , unsigned char col)
{
  unsigned char dez, uni, cen, mil, dmil, cmil,mi;


  if(valor<0)
  {
      valor*=(-1);
      if(menu==0x05 || menu==0x06)
         lcd_out (lin,col,"-");
      else
         lcd_out (lin,col," ");
   }
   else
   {
   if(menu==0x05 || menu==0x06)
         lcd_out (lin,col,"+");
      else
         lcd_out (lin,col," ");
   
   }

     
  mi   =            valor/1000000;
  cmil = (valor%1000000)/100000;
  dmil = (valor%100000)/10000;
  mil  = (valor%10000)/1000;
  cen  = (valor%1000)/100;
  dez  = (valor%100)/10;
  uni  = (valor%10);


{
  if(valor>999999)
  {  
     lcd_chr_cp (mi+48);
     lcd_Out_cp (".");
  }
  if(valor>99999)  lcd_chr_cp (cmil+48);
  if(valor>9999)  lcd_chr_cp (dmil+48);
  if(valor>999)
  { 
      lcd_chr_cp (mil+48);
      lcd_Out_cp (".");
  }
  if(valor>99) lcd_chr_cp (cen+48);
  if(valor>9)lcd_chr_cp (dez+48);
  lcd_chr_cp (uni+48);
 }





}

//*************************************************
void menu_0()    // mostra as opções de menu
{
 //   lcd_Out (1,1,"->");

    if(encoder>4) encoder=1;
    if(encoder<1) encoder=1;
    if(encoder==1)
    {

      pisca_lcd("Periodo>:",2,1);
      //lcd_out(1,1,"Periodo>:");
      mostra_lcd(periodo*mult_periodo,2,10);
      lcd_Out_cp("ms");          //1
      lcd_Out (3,1,"Ciclo Ativo    ");    //2
    }
    else if (encoder==2)
    {
      pisca_lcd("Cic.Atv>",2,1);;    //2
    //  lcd_out(1,1,"Cic.Atv>");
      lcd_Out_cp("        ");
      lcd_Out (3,1,"Disparo:");   //3
      mostra_lcd(disparo,3,9);
      lcd_Out_cp("        ");
    }
    else if (encoder==3)
    {
      pisca_lcd("Disparo>",2,1);    //3
    //  lcd_out(1,1,"Disparo>");
      mostra_lcd(disparo,2,11);
      lcd_Out (3,1,"Iniciar Teste  ");
    }
    else if (encoder==4)
    {
      pisca_lcd("Iniciar Teste>",2,1);;    //3
      //lcd_out(1,1,"Iniciar Teste>");
      lcd_Out_cp("        ");
      lcd_Out (3,1,"Periodo:");
      mostra_lcd(periodo*mult_periodo,3,9);
      lcd_Out_cp("ms");
    }

    if(flags.B2) // pressionou SW
    {
                 flags.B2=0x00;
                Lcd_Cmd(_LCD_CLEAR);
                 menu=encoder;
                 milisec_dis=0x00;
                 if(encoder==1) encoder = periodo;
                 else if (encoder==3) encoder = disparo;
                 else if (encoder==2) encoder = ciclo;
     }

    return;

}
//*************************************************
void conf_periodo()        //menu=1
{
   lcd_Out (2,1,"Ajusta Peiodo   ");
   lcd_Out (3,1,"P:");
   mostra_lcd(encoder*mult_periodo,3,3);
   lcd_out_cp(" ms        ");
   if(flags.B2) // pressionou SW
    {
                 flags.B2=0x00;
                 periodo=encoder;
                 EEPROM_write(0x00,periodo);
                 delay_ms(10);
                 menu=0x00;
                 lcd_Out (2,1,"***CONFIRMADO***");
                 lcd_Out (3,1,"Periodo:        ");
                  mostra_lcd(periodo*mult_periodo,3,9);
                  lcd_out_cp(" ms");
                 delay_ms(2000);
                 Lcd_Cmd(_LCD_CLEAR);
    }
    return;

}
//*************************************************
void conf_ciclo()        //menu=2
{
   lcd_Out (2,1,"Ajusta Ciclo Ativo");
   lcd_Out (3,1,"C:");
   mostra_lcd(encoder,3,3);
   lcd_Out_cp (" %         ");
   if(encoder>99) encoder=99;

   if(flags.B2) // pressionou SW
    {
                 flags.B2=0x00;
                 ciclo=encoder;
                 encoder=0x04;
                 menu=0x00;

    }
    return;

}
//*************************************************
void conf_disparo()       //menu=3
{
   lcd_Out (2,1,"Disparar no LED ");
   lcd_Out (3,1,"Nr: ");
   mostra_lcd(encoder,3,5);
   lcd_out_cp("          ");
   if(flags.B2) // pressionou SW
    {
                 flags.B2=0x00;
                disparo=encoder;
                EEPROM_write(0x01,disparo);
                 delay_ms(10);
                 encoder=0x04;
                 menu=0x00;
                 lcd_Out (2,1,"***CONFIRMADO***");
                 lcd_Out (3,1,"Disparo:        ");
                  mostra_lcd(disparo,3,9);
                  Lcd_Chr_Cp(223);
                  lcd_out_cp(" LED");
                 delay_ms(2000);
                 Lcd_Cmd(_LCD_CLEAR);
    }
    return;

}
//*************************************************
void iniciar()       //menu=4   //prepara para iniciar o teste - aguarda pressionar botaão iniciar/parar
{
  //pisca_lcd("PRESS. INICIAR.." ,1 , 1);
   lcd_Out (2,1,"PRESS. INICIAR..");
   lcd_Out (3,1,"Periodo:");
   mostra_lcd(periodo*mult_periodo,3,9);
   lcd_Out (2,15,"ms");
   if(!flags.B4) // se teste ainda não foi iniciado
   {
      //  Lcd_Cmd(_LCD_CLEAR);
        milisec=0X00;
        iter=0x00;
        PORTC=0x00;
      //  varaux=0x01;     //----comentar. Rotina de teste
        milisec_dis=periodo*mult_periodo*disparo;
        //flags.B4=0x01;   //teste iniciado
    }
    if(flags.B2) // pressionou SW
    {
                 flags.B2=0x00;
                 encoder=0x04;
                 menu=0x00;
                 Lcd_Cmd(_LCD_CLEAR);
    }
    return;
}

//*************************************************
void configuracoes()
{

  if(flags.B1 && milisecaux>200) flags.B1=0x00;   // rotina anti bouncing do sw do encoder
}
//**************************************************

void fboot()
{
  const unsigned long dela=50;
  lcd_Out (2,2,"I");
  delay_ms(dela);
  lcd_Out_cp("F");
  delay_ms(dela);
  lcd_Out_cp(" ");
  delay_ms(dela);
  lcd_Out_cp("S");
  delay_ms(dela);
  lcd_Out_cp("U");
  delay_ms(dela);
  lcd_Out_cp("D");
  delay_ms(10);
  lcd_Out_cp("E");
  delay_ms(dela);
  lcd_Out_cp("S");
  delay_ms(dela);
  lcd_Out_cp("T");
  delay_ms(dela);
  lcd_Out_cp("E");
  delay_ms(dela);
  lcd_Out_cp("-");
  delay_ms(dela);
  lcd_Out_cp("M");
  delay_ms(dela);
  lcd_Out_cp("G");
  delay_ms(dela);
  lcd_Out (2,1,"C");
  delay_ms(dela);
  lcd_Out_cp("A");
  delay_ms(dela);
  lcd_Out_cp("M");
  delay_ms(dela);
  lcd_Out_cp("P");
  delay_ms(dela);
  lcd_Out_cp("U");
  delay_ms(dela);
  lcd_Out_cp("S");
  delay_ms(dela);
  lcd_Out_cp(" ");
  delay_ms(dela);
  lcd_Out_cp("R");
  delay_ms(dela);
  lcd_Out_cp("I");
  delay_ms(dela);
  lcd_Out_cp("O");
  delay_ms(dela);
  lcd_Out_cp(" ");
  delay_ms(dela);
  lcd_Out_cp("P");
  delay_ms(dela);
  lcd_Out_cp("O");
  delay_ms(dela);
  lcd_Out_cp("M");
  delay_ms(dela);
  lcd_Out_cp("B");
  delay_ms(dela);
  lcd_Out_cp("A");
  delay_ms(3000);
  ver_batt();
  delay_ms(3000);

  Lcd_Cmd(_LCD_CLEAR);               // Clear display

}

//******************************************************
void pisca_lcd(char *msg,unsigned char lin , unsigned char col)
{
  if(!flags.B5)
  {   //  LATE2_bit=0X01;
     lcd_out(lin,col,msg);
  }
  else
  {
   // LATE2_bit=0X00;
      lcd_out(lin,col,"               ");
  }
}

//******************************************************
void ver_batt()
{
  float adc_batt;
  float vbat;
  unsigned int porc=0;

  char texto_vbat[5];  /* "99.9" + terminador */
unsigned int decimos;
unsigned int inteira;
unsigned short pos;



  int aux;
  milisec_batt=0x00;
  TRISA.B5= 0x00;
  delay_ms(10);
  PORTA.B5 = 0x01;
  adc_batt = (float)ADC_Read(0);
  Delay_ms(10);
  aux = ADC_Read(0);
  Delay_ms(10);
  /*
  **** calculo tensão batria
       Y = A + B1*X + B2*X^2
       A	10.32556
       B1	-0.00662
       B2	2.00483E-6

*/
  vbat = 10.32556
     - (0.00662 * adc_batt)
     + (2.00483E-6 * adc_batt * adc_batt);
 
 //**** conversão vbat para char
 decimos = (unsigned int)(vbat * 10.0 + 0.5);
 inteira = decimos / 10;
 pos = 0;
 if (inteira >= 10) {          // verifica se precisa escrever o código das dezenas
    texto_vbat[pos++] = '0' + inteira / 10;   // '0' ->48
    }
 texto_vbat[pos++] = '0' + inteira % 10;    // escrve a unidade
 texto_vbat[pos++] = '.';
 texto_vbat[pos++] = '0' + decimos % 10;    // escreve a casa  decimal
 texto_vbat[pos] = '\0';                    // terminador da string

 //**** cálculo da porcentagem de carga
 if(adc_batt>=2980)  porc=100;
 else if (adc_batt<=2120)  porc=0;
 else porc = (long)((adc_batt-2112)*10/87);

 TRISA.B5= 0x01; //    coloca A5 em entrada

 //****mostra valor no LCD
  Lcd_Cmd(_LCD_CLEAR);               // Clear display
 lcd_out(1,1,"Bat:");
 lcd_out(1,14,"V");

 lcd_out(2,1,"PORC: (");
 lcd_out(2,14,"%");
 
 lcd_out(3,1,"ADC:");

 lcd_out(1,6,texto_vbat);
 mostra_lcd(porc,2,6);
 mostra_lcd(aux,3,6);
 milisec_batt=0x00;

}