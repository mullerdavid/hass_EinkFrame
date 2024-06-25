# hass_EinkFrame

Eink Calendar and TODO list.
Touch sensor to switch between pages.

## Hardware

[Soldered Inkplate 6 (v2)](https://soldered.com/product/soldered-inkplate-6-6-e-paper-board/) was used. 
The v2 does not have touchpad, so a TTP223 was added to the GPIO13 as input.

[Images](#images)

## Software and Installation

Home Assistant and ESPHome.

### Dependencies

For the calendars and tasks, [O365 integration](https://github.com/RogerSelwyn/O365-HomeAssistant) was installed to HA.
For the weather, the built-in [OpenWeatherMap integration](https://www.home-assistant.io/integrations/openweathermap/) was used. 
For the fonts, [Verdana](https://learn.microsoft.com/en-us/typography/font-list/verdana) and [Material Design Icons](https://github.com/google/material-design-icons) were used.

### Secrets

Add your wifi credentials to the `secrets.yaml`.

	wifi_ssid: "wifi ssid"
	wifi_password: "wifi password"
	
### Template sensor

Add the template sensors to your `config.yaml`.

### ESPHome

Add the `esphome-web-*.yaml` and `esphome-web-*.hpp` files to your instance/device. Modify the yaml and code accordingly to change settings, fonts or language.

# Images

![image1](https://github.com/mullerdavid/hass_EinkFrame/blob/master/image1.png?raw=true)

![image2](https://github.com/mullerdavid/hass_EinkFrame/blob/master/image2.png?raw=true)