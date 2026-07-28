# Pi464
Pi464 is a Amstrad CPC 464 emulator developed for the Raspberry Pi Pico (RP2040) using the Arduino IDE environment (you can use also cpc6128 rom with 64Kb limit).
The project integrates support for the ILI9341 SPI display, game loading from SD card, PS/2 keyboard input, and PWM audio output.

# 🚀 Features
  -  Core: Emulation of Z80 CPU, GateArray video, ppi8255 and AY3891 audio.
  -  Display: ILI9341 320×240 SPI support.
  -  Storage: Loading .dsk files and .jpg previews from MicroSD.
  -  Audio: PWM sound generation with analog low‑pass filter.
  -  Input: single joystick ports (via GPIO) and PS/2 keyboard.


# 🛠️ Hardware Requirements
To build the project, you will need the following components:

 - Microcontroller: Raspberry Pi Pico (RP2040).
       <br/><img width="300" height="300" alt="pico" src="https://github.com/user-attachments/assets/57d94fe2-ba95-4041-8d19-b6f1106df1b2" />
     
 - Display: 2.8" ILI9341 SPI (320×240) with integrated SD reader.
       <br/><img width="640" height="200" alt="tft-display-2 8-spi" src="https://github.com/user-attachments/assets/6522d432-fa3d-4541-953e-193235030034" />

 - Audio:
    - Low‑pass filter (connected to a PWM GPIO).
    - PAM8403 amplifier (configured in mono mode).
          <br/><img width="300" height="225" alt="pam8403-1" src="https://github.com/user-attachments/assets/1a2d5370-dfe8-4518-98e7-7616ac8fd7c8" />

    - 3W / 4–8Ω speaker
          <br/><img width="300" height="300" alt="speaker" src="https://github.com/user-attachments/assets/8fcebdf6-4df0-4d92-b6e8-447f233d5c6f" />

 - Input:
    - PS/2 connector for a PC keyboard to use in Basic mode and game.
          <br/><img width="200" height="204" alt="0f380bbf3fce69dd9c6ff806902553cc" src="https://github.com/user-attachments/assets/071a68a5-559e-4113-a471-bac94645aa31" />

    - 1× directional joystick boards with 5‑way switch + 2 buttons.
          <br/><br/><img width="300" height="161" alt="COM-5WS-01" src="https://github.com/user-attachments/assets/ae02c59a-e821-4228-8e59-e08bd2625be7" />



# 🔌 Wiring
The Raspberry Pi Pico is the core of the system. Below are the detailed connections, divided by module.

## 1. ILI9341 Display & SD Card

The display and SD card use two separate SPI buses or dedicated pins to ensure maximum loading speed and video refresh rate.

| Component | Module Pin | Pico Pin (GPIO) | Note |
|----------|-------------|------------------|------|
| Display  | VCC         | VSYS             | Powered directly from Pico |
|          | GND         | GND              | |
|          | SDO (MISO)  | GP16             | |
|          | LED         | 3V3              | |
|          | SCK         | GP18             | |
|          | SDI (MOSI)  | GP19             | |
|          | DC          | GP21             | |
|          | RESET       | GP20             | |
|          | CS          | GP17             | |
| SD Card  | SD_CS       | GP13             | |
|          | SD_SCK      | GP10             | |
|          | SD_MOSI     | GP11             | |
|          | SD_MISO     | GP12             | |


## 2. Audio (PWM + Filter + Amplifier)

Audio is generated via PWM on pin GP15.<br/>
The signal passes through a passive low‑pass filter before entering the PAM8403 amplifier.<br/>
**Audio Filter Schematic**<br/>
1. PWM signal (GP15) → 1.5kΩ resistor
2. From the other end of the resistor:
    - 22nF capacitor to GND
    - 10µF capacitor to PAM8403 input
    - alternatively I used a two-stage filter as shown below:
  
                       R1 = 3.3 kΩ          R2 = 1 kΩ
          IN_PWM ----[ R1 ]----o----[ R2 ]----o----> IN PAM8403
                        V1     |      Vout    |
                             [ C1 ]         [ C2 ]
                             15 nF          47 nF
                               |              |
                              GND            GND
      Note: This cascade configuration is effective for converting a PWM signal into a clean analog (PAM) voltage for the input of the PAM8403 amplifier, while attenuating the high frequencies of the PWM carrier.
3. PAM8403 must be powered from an external 5V supply with a common GND shared with the Pico.

## 3. PS/2 Keyboard & System Buttons

The PS/2 keyboard is powered at 3.3V directly from the Pico.

| Device   | Function   | Pico Pin (GPIO) | Note |
|----------|------------|------------------|-----|
| Keyboard | PS2 DATA   | GP27 ||
|          | PS2 CLOCK  | GP28 ||
| Buttons  | REBOOT     | GP26 |extra mapped to the joystick|


## 4. Joystick (Port 1)

Joystick use internal pull‑up resistors.
Each button closes the contact to GND.

| Joystick   GPIO | Function |
|-----------------|----------|
| GP2             | UP |
| GP3             | DOWN |
| GP4             | LEFT |
| GP5             | RIGHT |
| GP6             | FIRE1 |
| GP26            | FIRE2 |


# 📝 Technical Notes

- Power:<br/>
    Pico powered via USB.<br/>
    Display uses VSYS (5V required for SD reader).<br/>
    PS/2 keyboard must support 3.3V, otherwise use voltage dividers.<br/>
    Audio amplifier requires dedicated 5V to avoid noise.<br/>

- Audio:<br/>
    Filter cutoff ≈ 4.8kHz (low, but reduces ticks/noise).<br/>

- TFT_eSPI Configuration:<br/> 
    Replace User_Setup.h with:<br/>
        - Driver: ILI9341<br/>
        - SPI Frequency: 80MHz<br/>
        - DMA: Enabled<br/>

If you notice graphical glitches, reduce SPI frequency to 40–50MHz.

# 📂 Software Setup

The project is developed for Arduino IDE using the RP2040 core.<br/> 
## Required Libraries

Make sure you have installed:<br/> 
    - TFT_eSPI (configured for ILI9341 on Pico)<br/> 
    - TJpg_Decoder (for game previews)<br/> 
    - SD and LittleFS<br/> 

## SD Card Structure

The emulator looks for files inside the /Pi64 directory. It can work with games that require expansions.<br/>
Organize your SD card like this:<br/>

        Pi464/
        ├─ disks/
        │   ├─ game1.dsk
        │   ├─ game1.jpg
        │   ├─ game2.dsk
        │   ├─ game2.jpg
        │     ...
        │   └─ noImage.jpg
        
Images must be 200x150px jpg no progressive format.<br/>

## Using LittleFS Instead of a Physical SD Card

Instead of using a physical SD card, you can store the entire Pi64 folder directly inside the Pico’s onboard flash memory.<br/>
To do this, use the LittleFS Upload Tool for Arduino IDE, which uploads the contents of your local /data folder into the Pico’s flash filesystem.<br/>
Official documentation:<br/>
Arduino-Pico LittleFS guide:<br/>
https://arduino-pico.readthedocs.io/en/latest/filesystems.html (arduino-pico.readthedocs.io in Bing)<br/>
LittleFS Upload Tool (arduino-littlefs-upload):<br/> 
https://github.com/earlephilhower/arduino-littlefs-upload (github.com in Bing)<br/>

# 🎮 Usage

1. **Startup**: On boot, the main menu “CPCMenu” appears.<br/>
2. **Navigation**: Use the joysticks to move between options:<br/>
    **BASIC**: Launches the standard BASIC interpreter.<br/>
    **LOAD DISK**:   Opens the file browser to load a .dsk from SD.<br/>
3. **Disk Browser**: Scroll through the disk list with the joystick; press Fire to load the selected .dsk.<br/>
When you load a disk and want to run the programs on it, type `cat` and press Enter at the BASIC interface. You will be able to see the contents of the disk you just loaded. Type `run"filename"` to run the chosen program.

# ⚡ Performance & Overclock

To achieve acceptable emulation speed, the Raspberry Pi Pico is pushed beyond its factory specifications.<br/>
In configs.h:<br/>

            #define PICO_CLOCK_KHZ 276000 // Overclock to 276MHz
            
According to available documentation, 276MHz is generally achievable without special precautions.<br/>
This frequency allows:<br/>
    - SPI bus at 80MHz for the display<br/>
    - Acceptable performance for Z80 and audio.<br/>
You may still notice slowdowns in some games, graphical glitches, or audio ticks due to timing desynchronization.<br/>

# ⚠️ Disclaimer

This project is provided “as is”, for hobby and experimental use.<br/>
Overclock: May reduce microcontroller lifespan or cause thermal instability.<br/>
Hardware: The author is not responsible for damage to components (Pico, display, speaker), overheating, or malfunctions caused by incorrect wiring or use of the provided software.<br/>
Risks: Any hardware or software modification is performed at the user’s own risk. 
