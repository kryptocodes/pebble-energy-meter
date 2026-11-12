var Clay = require('pebble-clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// ---- Weather via Open-Meteo (free, no API key) ----
var CONDITIONS = function (code, isDay) {
  if (code === 0 || code === 1) return isDay ? 'SUNNY' : 'CLEAR';
  if (code === 2 || code === 3) return 'CLOUDY';
  if (code === 45 || code === 48) return 'FOG';
  if (code >= 51 && code <= 57) return 'DRIZZLE';
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return 'RAIN';
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return 'SNOW';
  if (code >= 95) return 'STORM';
  return '';
};

function useFahrenheit(fromWatch) {
  if (fromWatch !== undefined) return !!fromWatch;
  try {
    var saved = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    return saved.TempUnit === 'F';
  } catch (e) {
    return false;
  }
}

function fetchWeather(unitFromWatch) {
  var fahrenheit = useFahrenheit(unitFromWatch);
  navigator.geolocation.getCurrentPosition(function (pos) {
    var url = 'https://api.open-meteo.com/v1/forecast' +
      '?latitude=' + pos.coords.latitude.toFixed(2) +
      '&longitude=' + pos.coords.longitude.toFixed(2) +
      '&current=temperature_2m,weather_code,is_day,relative_humidity_2m' +
      '&daily=temperature_2m_max,temperature_2m_min,sunset&timezone=auto&forecast_days=1' +
      (fahrenheit ? '&temperature_unit=fahrenheit' : '');
    var xhr = new XMLHttpRequest();
    xhr.onload = function () {
      try {
        var data = JSON.parse(this.responseText);
        var c = data.current;
        var d = data.daily;
        Pebble.sendAppMessage({
          Temperature: Math.round(c.temperature_2m),
          Conditions: CONDITIONS(c.weather_code, c.is_day === 1),
          TempHigh: Math.round(d.temperature_2m_max[0]),
          TempLow: Math.round(d.temperature_2m_min[0]),
          Humidity: Math.round(c.relative_humidity_2m),
          Sunset: d.sunset[0].slice(11, 16)
        });
      } catch (e) {
        console.log('weather parse failed: ' + e);
      }
    };
    xhr.open('GET', url);
    xhr.send();
  }, function (err) {
    console.log('location failed: ' + err.message);
  }, { timeout: 15000, maximumAge: 60 * 60 * 1000 });
}

Pebble.addEventListener('ready', function () {
  fetchWeather();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload.RequestWeather) fetchWeather(e.payload.TempUnit);
});
