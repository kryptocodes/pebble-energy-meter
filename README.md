# Energy Meter

A Pebble watchface that looks like the electricity meter on the wall at home.

The register drums show the time as HH MM SS, the disc spins in its slot, and the CAL LED blinks every second. The nameplate shows weather, steps, calories, day, date and battery. The sticker in the corner turns red when the phone disconnects.

Built for the Pebble Time 2 but runs on basalt, chalk and diorite too.

<img src="screenshots/emery.png" width="200"> <img src="screenshots/emery-saver.png" width="200">

## Settings

Everything is set from the Pebble app:

- nameplate text (default `PEBBLE TIME`, or put `PROPERTY OF TNEB` etc.)
- ratings row: live (weather / steps / kcal) or classic meter ratings
- disc speed follows your step rate and heart rate
- smooth disc spin on/off
- accent colour, °C / °F
- LED blink, vibrate on disconnect
- battery saver: off / auto below a set % / always

In saver mode the face only updates once a minute. Seconds show as a dash, the disc moves once a minute, and the LED label changes to ECO. Flick your wrist to see seconds for 30 s.

## Build

```
pebble build
pebble install --emulator emery
pebble install --phone <phone-ip>
```

Weather comes from [Open-Meteo](https://open-meteo.com), no API key needed. Drum font is Arimo Bold.
