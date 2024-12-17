
# Minimalistic E-Paper 4.2inch Display Weather Station

## Hardware
1. Xiao ESP32 C3
2. Waveshare 4.2inch TriColor 400x300 E-paper display refresh time 15 secs.
3. TMP117 (For temperature)
4. BH1750 (For light sensing)
5. DS3231 (For time keeping)
6. BMP680 (For humidity & pressure)
7. ICR (LiCoO2) 2x2200mAh Lithium Battery (Simple 18650 batteries)
8. 2xBMS (3.7V) CuttOff at 2.5V or Higher
9. Other stuffs like wire, capacitor (104), resistor (1M Ohm), connectors, switch, LED (1) etc.

## UPDATES V0.1.2-alpha
[Changelog](https://github.com/KamadoTanjiro-beep/ESP32-ePaper-Display-Weather-Clock/commit/05175a90e57118196dd1360790bf2677fd99840c)
## FEATURES & UPDATES V0.1.1-alpha
[Changelog](https://github.com/KamadoTanjiro-beep/ESP32-ePaper-Display-Weather-Clock/commit/5dc709a0e5b343795bc190758548002f35fe8f6f)
## FEATURES V0.1.0-alpha
1. Shows Room Temperature using Medical Grade Temperature Sensor TMP117.
2. Humidty and Pressure using Bosch BME680
3. Tracks High and Low
4. Shows battery percentage and voltage.
5. Needs OpenWeatherMap API key for fetching Outdoor Weather Data (OPTIONAL)
6. Moon Phase, AQI, Sunrise, Sunset, Real Feel, Weather Data
7. No need to modify the code, just upload it and it will automatically ask you to configure WIFI Settings using PC or Mobile, just chill.
8. Houses a DS3231 for internal time trackings
9. Clock updates every 15 mins and sleeps for the rest. (Configurations via web server coming soon, till then modify code)
   
## UPCOMING FEATURES
Check [issues](https://github.com/KamadoTanjiro-beep/epdWeatherClockV1/issues)

## Other Info

Clock based on Waveshare 4.2inch e-Paper Module (B) 3-Colour, 400 × 300. :leaves: Eco-Friendly!

The clock runs on a 2 x 2200mAh (4400mAh total) LiCoO2 cell ( :leaves: Not so Eco-Friendly!). 

Also, it houses a LUX sensor (BH1750) for sleeping while it is dark (E-paper doesn't have a backlight, remember?) ( :leaves: :leaves: :leaves: Eco-Friendly af faka fak!)

Made of old delivery card boards. :exploding_head: One-Punch Eco-Friendly Boost Ultra Pro Max :leaves: to :infinity:

Supports 5V 1000mA Charging (TP5000 Module)

USB-C based operation.

This clock runs on GxEPD2 Library and others. Weather icon credits: SeBassTian23

Battery life: <br>
a. Then: Full: 06/05/2024, Dead: 17/07/2024 So, 73 days or 2 month 12 Days.
b. 2nd Charge: 49 Days, 3rd Charge: 48 Days

## Picture(s)

![Clock](https://github.com/KamadoTanjiro-beep/epdWeatherClockV1/blob/main/epd.jpg)


## License

Distribute it freely but link back to this project or put some good words or attributes or donate (paypal link in profile) haha. You are own your own, I take no resposibility, if this thing explodes or does any damage on anything.

