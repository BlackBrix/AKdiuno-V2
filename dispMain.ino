void dispMain(bool full_redraw)
  {
      int16_t x, y;                             // Helper placement coordinates
      int16_t  x1, y1;                          // Coordinates of top left corner of blanking rectangle.
      uint16_t w, h;                            // Width and height of blanking rectangle.

      if (full_redraw == true)
        {
          tft.fillScreen(BLACK);
        }
      
      // **************************************** Display Volume ************************************************
      
      if ((volume != volumeOld) || (full_redraw == true))
        {
          tft.setFont(&Arimo_106);
          tft.setTextColor(WHITE);    
        
          float VolFloat = -1*((volume-255)/2);
          int VolDB = (int)VolFloat;
          
          if (volume <= 235)                                          // Determine if volume is 2 digits or one.
            {
              Vol2Digit = true;
            }
          else Vol2Digit = false;
      
          if (full_redraw == true)                                  // If full redraw, erase the full Volume space
            {
              tft.fillRect(90, 0, 300, 83, BLACK);
            }

          if (full_redraw == true)                                  // ------------ Print the "-" symbol --------------
            {
              if (Vol2Digit == false)
                {
                  tft.setCursor(110, 80);                            // If single digit
                }
              else tft.setCursor(90, 80);                           // If two digits
              tft.println("-");
            }

          if (full_redraw == false)                                  // ------------ When switching from one to two digits, move the "-" symbol -------------
            {
              if ((Vol2Digit == false) && (Vol2Digit != Vol2DigitOld))
                {
                  tft.fillRect(90, 0, 30, 83, BLACK);               // If two digits
                  tft.setCursor(110, 80);                          // If single digit
                  tft.println("-");
                }
              else if ((Vol2Digit == true) && (Vol2Digit != Vol2DigitOld))
                {
                  tft.fillRect(110, 0, 30, 83, BLACK);               // If two digits
                  tft.setCursor(90, 80);                        // If two digits
                  tft.println("-");
                }
            }
                    
          if (Vol2Digit == false)                                  // ------------ Print the actual volume -------------
            {
              tft.fillRect(140, 0, 90, 83, BLACK);                // If single digit
              tft.setCursor(160, 80);                               // If single digit
            }
          else 
            {
              tft.fillRect(130, 0, 120, 83, BLACK);               // If two digits
              tft.setCursor(130, 80);                            // If two digits
            }
          tft.println(VolDB);

          if (full_redraw == true)                                  // ------------ In case of full redraw, print the "dB" symbol -------------
            {
              if (Vol2Digit == false)
                {
                  tft.setCursor(230, 80);                          // If single digit
                }
              else tft.setCursor(255, 80);                        // If two digits
              tft.println("dB");
            }

          if (full_redraw == false)                                  // ------------ When switching from one to two digits, move the "dB" symbol -------------
            {
              if ((Vol2Digit == false) && (Vol2Digit != Vol2DigitOld))
                {
                  tft.fillRect(230, 0, 150, 83, BLACK);               // If two digits
                  tft.setCursor(230, 80);                          // If single digit
                  tft.println("dB");
                }
              else if ((Vol2Digit == true) && (Vol2Digit != Vol2DigitOld))
                {
                  tft.fillRect(250, 0, 140, 83, BLACK);               // If two digits
                  tft.setCursor(255, 80);                        // If two digits
                  tft.println("dB");
                }
            }

          volumeOld = volume;
          Vol2DigitOld = Vol2Digit;
        }

      // **************************************** Display Signal Type ************************************************
      
      if ((dsdSignal != dsdSignalOld) || (full_redraw == true))
        {
          x = 20;
          y = 160;
          tft.setFont(&Arimo_Bold_64);
          tft.getTextBounds("DSD ", x, y, &x1, &y1, &w, &h);
          tft.fillRect(x1, y1, w, h, BLACK);
          if (dsdSignal == 1)                     // If signal is DSD
            {
              tft.fillRect(20, 100, 440, 70, BLACK);
            }
          else if (dsdSignal == 0)                     // If signal is PCM
            {
              tft.fillRect(20, 100, 440, 70, BLACK);
              tft.setCursor(x, y);
              tft.println("PCM");
            }
        }

      // **************************************** Display Sampling Rate ************************************************
      
      if ((SR_I2S != SR_I2SOld) || (full_redraw == true))
        {
          if (dsdSignal == 0)                     // If signal is PCM
            {
              x = 200;
              y = 160;
              tft.setFont(&Arimo_Bold_64);
              tft.getTextBounds(mySR[SR_I2SOld], x, y, &x1, &y1, &w, &h);
              tft.fillRect(x1, y1-2, w+5, h+3, BLACK);
              tft.setCursor(x, y);
              tft.println(mySR[SR_I2S]);
            }
          else if (dsdSignal == 1)                     // If signal is DSD
            {
              if (SR_I2S == 5)
                {
                  x = 135;
                }
              else x = 120;
              y = 160;
              tft.setFont(&Arimo_Bold_64);
              tft.fillRect(20, 100, 440, 70, BLACK);
              
              tft.setCursor(x, y);
              tft.println(mySR[SR_I2S]);
            }
        }


      // **************************************** Display DAC filter ************************************************
      
      if ((dacFilter != dacFilterOld) || (full_redraw == true))
        {
          tft.setFont(&Arimo_Bold_32);
          tft.getTextBounds(myFilters[dacFilter], 0, 270, &x1, &y1, &w, &h);
          tft.fillRect(0, 240, 480, 40, BLACK);
          tft.setCursor((480-w)/2, 270);
          tft.println(myFilters[dacFilter]);
          dacFilterOld = dacFilter;
        }

      // **************************************** Display MCLK frequency ************************************************
      
      if ((MCLK != MCLKOld) || (full_redraw == true))
        {
          tft.setFont(&Arimo_Bold_32);
          tft.getTextBounds("MCLK set to 45.1584 MHz", 0, 315, &x1, &y1, &w, &h);
          tft.fillRect(0, 290, 480, h+5, BLACK);
          tft.setCursor((480-w)/2, 315);
          if (MCLK == 1)
            {
              tft.println("MCLK set to 11.2896 MHz");
            }
          else if (MCLK == 2)
            {
              tft.println("MCLK set to 12.288 MHz");
            }
          else if (MCLK == 3)
            {
              tft.println("MCLK set to 16.384 MHz");
            }
          else if (MCLK == 4)
            {
              tft.println("MCLK set to 16.9344 MHz");
            }
          else if (MCLK == 5)
            {
              tft.println("MCLK set to 18.432 MHz");
            }
          else if (MCLK == 6)
            {
              tft.println("MCLK set to 22.5792 MHz");
            }                                              
          else if (MCLK == 7)
            {
              tft.println("MCLK set to 24.576 MHz");
            }
          else if (MCLK == 8)
            {
              tft.println("MCLK set to 33.8688 MHz");
            }
          else if (MCLK == 9)
            {
              tft.println("MCLK set to 36.864 MHz");
            }
          else if (MCLK == 10)
            {
              tft.println("MCLK set to 45.1584 MHz");
            }
          else if (MCLK == 11)
            {
              tft.println("MCLK set to 49.152 MHz");
            }
          MCLKOld = MCLK;
        }
  }

