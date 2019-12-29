""" Support for MultiHTU21D sensors.
Support for getting information from MultiHTU21D.
12 April 2019.

Licensed under the terms of the GPL version 3.
http://www.gnu.org/licenses/gpl-3.0.html
Copyright(c) 2013-2016 by Artem Khomenko _mag12@yahoo.com.
"""

import logging

import voluptuous as vol # pylint: disable=import-error

from homeassistant.components.sensor import PLATFORM_SCHEMA # pylint: disable=import-error
from homeassistant.helpers.entity import Entity # pylint: disable=import-error
import homeassistant.helpers.config_validation as cv # pylint: disable=import-error
from homeassistant.const import (TEMP_CELSIUS, # pylint: disable=import-error
                                 DEVICE_CLASS_HUMIDITY,
                                 DEVICE_CLASS_TEMPERATURE)
import custom_components.multiHTU21D as multiHTU21D # pylint: disable=import-error

_LOGGER = logging.getLogger(__name__)

CONF_SENSORS = 'sensors'
CONF_NAME_TEMP = 'name_temperature'
CONF_NAME_HUMI = 'name_humidity'

DEPENDENCIES = ['multiHTU21D']

SENSORS_SCHEMA = vol.Schema({
   vol.Required(CONF_NAME_TEMP): cv.string,
   vol.Required(CONF_NAME_HUMI): cv.string
})

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
   vol.Required(CONF_SENSORS):
       vol.Schema({cv.positive_int: SENSORS_SCHEMA}),
})


async def async_setup_platform(hass, config, async_add_entities,
                               discovery_info=None):
    """Set up the Arduino platform."""
    if multiHTU21D.BOARD is None:
        _LOGGER.error(
            "A connection has not been made to the multiHTU21D board")
        return False

    sensorsConf = config.get(CONF_SENSORS)

    sensors = []
    for sensNum, sens in sensorsConf.items():
        sensors.append(multiHTU21DSensor(sens.get(CONF_NAME_TEMP), sensNum, DEVICE_CLASS_TEMPERATURE, TEMP_CELSIUS))
        sensors.append(multiHTU21DSensor(sens.get(CONF_NAME_HUMI), sensNum, DEVICE_CLASS_HUMIDITY, '%'))

    sensors.append(multiHTU21DSensorExt("Voltage", "Voltage", "V"))
    sensors.append(multiHTU21DSensorExt("Power", "Power", "W"))
    async_add_entities(sensors)


class multiHTU21DSensor(Entity):
    """Representation of an Arduino Sensor."""

    def __init__(self, name, sensorNumber, device_class,
                 unit_of_measurement):
        """Initialize the sensor."""
        self._name = name
        self._state = None
        self._device_class = device_class
        self._unit_of_measurement = unit_of_measurement
        self._sensorNumber = sensorNumber
        #self.async_update()

    @property
    def should_poll(self):
        return True

    @property
    def device_class(self):
        """Return the device class of the sensor."""
        return self._device_class

    @property
    def name(self):
        """Return the name of the sensor."""
        return self._name

    @property
    def unit_of_measurement(self):
        """Return the unit this state is expressed in."""
        return self._unit_of_measurement

    @property
    def state(self):
        """Return the state of the sensor."""
        return None if self._state is None else f'{self._state:0.2f}'

    async def async_update(self):
        """Get the latest data and updates the states. Update only once for 16 sensors. """
        #await multiHTU21D.BOARD.update()
        await self.hass.async_add_job(multiHTU21D.BOARD.update)

        #_LOGGER.error("update() = %s", self._value)
        self._state = multiHTU21D.BOARD.get_temperature(
            self._sensorNumber) if self._device_class == DEVICE_CLASS_TEMPERATURE else multiHTU21D.BOARD.get_humidity(self._sensorNumber)

class multiHTU21DSensorExt(multiHTU21DSensor):
    """Representation of an Arduino Sensor - extension for voltage and power."""

    def __init__(self, name, device_class,
                 unit_of_measurement):
        super().__init__(name, 0, device_class,
                 unit_of_measurement)

    async def async_update(self):
        #_LOGGER.error("update() = %s", self._value)
        self._state = multiHTU21D.BOARD.get_voltage() if self._device_class == "Voltage" else multiHTU21D.BOARD.get_power()
