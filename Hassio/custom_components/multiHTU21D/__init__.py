""" Support for MultiHTU21D sensors.
Main platform file.
12 April 2019.

Licensed under the terms of the GPL version 3.
http://www.gnu.org/licenses/gpl-3.0.html
Copyright(c) 2013-2021 by Artem Khomenko _mag12@yahoo.com.
"""

import logging
import voluptuous as vol                                    # pylint: disable=import-error
import serial
from datetime import timedelta
import array
import sys


from homeassistant.const import (                           # pylint: disable=import-error
    EVENT_HOMEASSISTANT_START, EVENT_HOMEASSISTANT_STOP)
from homeassistant.const import CONF_PORT                   # pylint: disable=import-error
import homeassistant.helpers.config_validation as cv        # pylint: disable=import-error
from homeassistant.util import Throttle # pylint: disable=import-error

REQUIREMENTS = ['pyserial==3.4']

_LOGGER = logging.getLogger(__name__)

BOARD = None

DOMAIN = 'multiHTU21D'

CONFIG_SCHEMA = vol.Schema({
    DOMAIN: vol.Schema({
        vol.Required(CONF_PORT): cv.string,
    }),
}, extra=vol.ALLOW_EXTRA)

MIN_TIME_BETWEEN_UPDATES = timedelta(seconds=5)

async def async_setup(hass, config):
    """Set up the Arduino component."""
    port = config[DOMAIN][CONF_PORT]
 
    global BOARD
    
    try:
        BOARD = await hass.async_add_job(ArduinoBoard, port)

    except (serial.serialutil.SerialException, FileNotFoundError):
        _LOGGER.error("Your port %s is not accessible", port)
        return False

    def stop_arduino(event):
        """Stop the Arduino service."""
        BOARD.disconnect()

    def start_arduino(event):
        """Start the Arduino service."""
        hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, stop_arduino)

    hass.bus.async_listen_once(EVENT_HOMEASSISTANT_START, start_arduino)

    return True


class ArduinoBoard:
    """Representation of an Arduino board."""

    def __init__(self, port):
        """Initialize the board."""
        self._port = port
        self._ser = None
        self._temperatures = None
        self._humidities = None
        self._voltage = None
        self._power = None

        # Open and update sensor values.
        self.update()

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update(self):
        """Read raw data and calculate temperature and humidity."""

        import math

        # If not connected, try to connect
        if self.checkPort() == False:
            return

        # Communicate with port - send command to get data and get answer.
        try:
            # Send request.
            self._ser.reset_input_buffer()
            self._ser.write(b'D')

            # Read 8 float values of temperature and 1 voltage.
            rawData = self._ser.read(4 * 9)
            if len(rawData) != 4 * 9:
                _LOGGER.warning("Error receive data from HTU21D, actually read %s bytes", len(rawData))
                return
            rawArray = array.array('f')
            rawArray.frombytes(rawData)

            # Round and replace error value on None
            self._temperatures = array.array('f')
            for i in range(8):
                self._temperatures.append(round(rawArray[i]) if rawArray[i] < 255.0 else math.nan)

            # Plus voltage
            self._voltage = round(rawArray[8], 2) if rawArray[8] < 255.0 else math.nan
            

            # Read humidity and power
            rawData = self._ser.read(4 * 9)
            if len(rawData) != 4 * 9:
                _LOGGER.warning("Error receive data from HTU21D, actually read %s bytes", len(rawData))
                return
            rawArray = array.array('f')
            rawArray.frombytes(rawData)

            # Round and replace error value on None
            self._humidities = array.array('f')
            for i in range(8):
                self._humidities.append(round(rawArray[i]) if rawArray[i] < 255.0 else math.nan)

            # Plus power
            self._power = round(rawArray[8], 2) if rawArray[8] < 255.0 else math.nan

        #except IOError:
        except:
            e = sys.exc_info()[0]
            _LOGGER.warning(
                "Error communicate with port %s for multiHTU21DSensor: %s", self._port, e)
            self.disconnect()

    def checkPort(self):
        """Check port to be ready."""

        import time

       # If port already opened, do nothing
        if self._ser is not None:
            return True

        try:
            # Open port.
            self._ser = serial.Serial(port=self._port, baudrate=115200, timeout=3)

            # Wait for port to get ready
            time.sleep(3)

            # Success
            return True
            
        except IOError:
            _LOGGER.error(
                "Cannot to open port %s for multiHTU21DSensor with error %s", self._port, sys.exc_info()[0])
            return False

    def clearValues(self):
        """ Set values to default (no data) state."""
        self._ser = None
        self._temperatures = None
        self._humidities = None
        self._voltage = None
        self._power = None

    def disconnect(self):
        """Disconnect the board and close the serial connection."""
        try:
            self._ser.close()
        except:
            pass

        self.clearValues()

    def turnHeater(self, isOn):
        """Turn on/off heater"""
        if not self.checkPort():
            return
        self._ser.write(b'C' if isOn else b'E')

    def turnFan(self, isOn):
        """Turn on/off fan"""
        if not self.checkPort():
            return
        self._ser.write(b'S' if isOn else b'F')

    def get_temperature(self, index):
        """Return the temperature of the sensor with index."""
        return None if self._temperatures is None else self._temperatures[index - 1]

    def get_humidity(self, index):
        """Return the humidity of the sensor with index."""
        return None if self._humidities is None else self._humidities[index - 1]
 
    def get_voltage(self):
        """Return the input voltage."""
        return self._voltage

    def get_power(self):
        """Return the input power."""
        return self._power


