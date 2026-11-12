module.exports = [
  { "type": "heading", "defaultValue": "Energy Meter" },
  { "type": "text", "defaultValue": "Customise your meter face." },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Nameplate" },
      {
        "type": "radiogroup",
        "messageKey": "Theme",
        "label": "Meter style",
        "defaultValue": "india",
        "options": [
          { "label": "Indian static meter", "value": "india" },
          { "label": "European meter with test dial", "value": "euro" }
        ]
      },
      {
        "type": "input",
        "messageKey": "PlateText",
        "defaultValue": "PEBBLE TIME",
        "label": "Nameplate text",
        "description": "Shown above the ratings row, e.g. PROPERTY OF TNEB.",
        "attributes": { "maxlength": 31, "placeholder": "PEBBLE TIME" }
      },
      {
        "type": "radiogroup",
        "messageKey": "RatingsMode",
        "label": "Ratings row",
        "defaultValue": "live",
        "options": [
          { "label": "Live: weather | steps | kcal", "value": "live" },
          { "label": "Classic: 240 V | 5-30 A | 50 Hz | 3200 imp/kWh", "value": "classic" }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Disc" },
      {
        "type": "toggle",
        "messageKey": "DiscActivity",
        "label": "Speed follows activity",
        "description": "Spins faster as your step rate or heart rate goes up.",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "DiscSmooth",
        "label": "Smooth continuous spin",
        "description": "Turn off to save battery: the disc then moves in short steps each second.",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Battery" },
      {
        "type": "radiogroup",
        "messageKey": "PowerMode",
        "label": "Battery saver",
        "description": "Saver updates once a minute: seconds drums show a dash, the disc steps each minute, the LED stays off and \"CAL\" reads \"ECO\".",
        "defaultValue": "auto",
        "options": [
          { "label": "Off: full animation", "value": "normal" },
          { "label": "Auto: saver when battery is low", "value": "auto" },
          { "label": "Always on", "value": "saver" }
        ]
      },
      {
        "type": "slider",
        "messageKey": "SaverBelow",
        "label": "Auto saver below (%)",
        "defaultValue": 20,
        "min": 10,
        "max": 50,
        "step": 5
      },
      {
        "type": "toggle",
        "messageKey": "ShakeSeconds",
        "label": "Shake to show seconds",
        "description": "In saver mode, flick your wrist to run the seconds for 30 s.",
        "defaultValue": true
      }
    ]
  },
  {
    "type": "section",
    "items": [
      { "type": "heading", "defaultValue": "Look & feel" },
      {
        "type": "color",
        "messageKey": "AccentColor",
        "label": "Accent colour",
        "description": "Seconds wheel, warning bolt and CAL LED.",
        "defaultValue": "FF0000",
        "sunlight": true,
        "capabilities": ["COLOR"]
      },
      {
        "type": "radiogroup",
        "messageKey": "TempUnit",
        "label": "Temperature",
        "defaultValue": "C",
        "options": [
          { "label": "Celsius (\u00B0C)", "value": "C" },
          { "label": "Fahrenheit (\u00B0F)", "value": "F" }
        ]
      },
      {
        "type": "toggle",
        "messageKey": "LedBlink",
        "label": "CAL LED blinks every second",
        "defaultValue": true
      },
      {
        "type": "toggle",
        "messageKey": "VibeDisconnect",
        "label": "Vibrate when phone disconnects",
        "defaultValue": true
      }
    ]
  },
  { "type": "submit", "defaultValue": "Save" }
];
