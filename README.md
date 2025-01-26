# E92dashModEnder3Screen
Chaotic name, simple mission.  
I have the old LCD from my Creality Ender 3 Pro, which I don't need anymore, because I'm running it headless with Klipper and I want to use it to display some additional information on the dash of my BMW E92.  

The project should be compatible with all cars from the E9x series, but I only have access to an E92, which is why I named the repository like that.  

The project currently is in a **very** early stage and more information will be added *soon*.  

## Plan
The plan is to tap into the CAN-bus somewhere on the vehicle to gather interesting information.  
From my research it seems the best bus to tap into is the KCAN (Karosserie-CAN = Body-CAN), because it contains the most interesting information.  
The speed of the bus is 100 kbit/s and it can be recognized by its twisted pair of orange/green and green wires, where OR/GN stands for CAN-HIGH and GN for CAN-LOW (to be confirmed).  
On my vehicle the easiest place to tap into KCAN seems to be by the rear PDC (park distance control) module.  
This module might not be installed on your car.  
Alternatives include the iDrive and ultrasonic overhead alarm sensor, which are both not installed nor prewired on my car.  
The only problem with the PDC is that it is mounted in the trunk of the car and I want the information on the dash of my car...  
But at least for testing it seems to be the most promising location to me.  
Again, this may vary depending on the options installed in your car.

## Schematic
The KiCAD schematic in this repository serves a purely symbolic purpose!  
I am not an expert at designing circuits and just want to give you a general idea about the correct wiring.  
Any constructive input on how to improve the schematic is greatly appreciated!

## Hardware
Here you can find a list of materials needed for the project.  
Note that this list always reflects the current state of the project and might change over time.  
Also note that links to products are only meant as a guide what to look for and what/where ***I*** bought them.  
Depending on your location/preference there might be better/cheaper alternatives.  
You can buy the components whereever you want as long as they are the same/similar enough.  
I am in no way affiliated with the linked sellers and can in no way guarantee their reliability!  

| Description | Quantity | Notes | Link |
|---|---|---|---|
| ESP32-WROOM-32 dev module | 1 | most other ESPs should also work, just connect everything to the right GPIOs | https://www.az-delivery.de/en/products/esp32-developmentboard?_pos=1&_psq=esp32+n&_ss=e&_v=1.0 |
| MCP2515 CAN bus module | 1 | yes, odd choice in combination with ESP32, I just use what I already had | https://www.az-delivery.de/en/products/mcp2515-can-bus-modul?_pos=1&_psq=MCP2515&_ss=e&_v=1.0 |
| Ender 3 (Pro) LCD | 1 | also an odd choice but, again, I already had one from my 3D printer; I will not leave a link where to buy one as this doesn't make much sense, it would be easier/cheaper to just implement support for other types of displays, which is also planned for the future! | |
| 10k Ohm resistor | 1 | used as a pullup, similar values may also work |  |
| 560 Ohm resistor | 2 | used for the transistors, similar values may also work |  |
| BC547 transistor | 1 | used to switch LCD on/off, can be replaced by a similar NPN |  |
| S8550 transistor | 1 | used to switch LCD on/off, can be replaced by a similar PNP |  |
| Jumper wires | 1 metric ton | will be replaced in the future by a less prototypie solution | |

## References
Here you can find some links with useful information in relation to this project.  

**CAN bus related**
| Description | Link |
|---|---|
| This thread contains **A LOT** of information about the E9x KCAN, but it is also kind of all over the place so you have to dig a bit | https://www.e90post.com/forums/showthread.php?t=177272 |
| Awesome work on decoding the BMW CAN bus system. But watch out, this work wasn't done on an E9X so CAN IDs and formulars *may* differ but should largely be the same. | https://www.loopybunny.co.uk/CarPC/k_can.html |
| A table I found with CAN bus IDs of the E9X and their meanings | https://github.com/kmalinich/node-bmw-ref/blob/master/canbus/e90-tool32-ids.csv |

**Hardware related**
| Description | Link |
|---|---|
| My source for the Ender3 LCD pinout | https://github.com/rfblock/ender3-lcd-arduino |
| Library used to drive LCD (can be downloaded via Arduino IDE) | https://github.com/olikraus/u8g2 |
| Library used to drive MCP2515 module (can be downloaded via Arduino IDE) | https://github.com/ttlappalainen/CAN_BUS_Shield |
