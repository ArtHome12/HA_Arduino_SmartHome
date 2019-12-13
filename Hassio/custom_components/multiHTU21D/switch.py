""" Support for MultiHTU21D sensors.
Control device - turn on/off heater and fan.
12 April 2019.

Licensed under the terms of the GPL version 3.
http://www.gnu.org/licenses/gpl-3.0.html
Copyright(c) 2013-2016 by Artem Khomenko _mag12@yahoo.com.
"""

import logging

import voluptuous as vol # pylint: disable=import-error

from homeassistant.components.switch import (SwitchDevice, PLATFORM_SCHEMA) # pylint: disable=import-error
from homeassistant.const import CONF_NAME # pylint: disable=import-error
import homeassistant.helpers.config_validation as cv # pylint: disable=import-error
import custom_components.multiHTU21D as multiHTU21D           


DEPENDENCIES = ['multiHTU21D']

_LOGGER = logging.getLogger(__name__)


async def async_setup_platform(hass, config, async_add_entities,
                               discovery_info=None):
    """Set up the Arduino platform."""
    # Verify that Arduino board is present
    if multiHTU21D.BOARD is None:
        _LOGGER.error("A connection has not been made to the multiHTU21D board")
        return False

    #pins = config.get(CONF_PINS)

    switches = [multiHTU21DHeaterSwitch('Heater'),
                multiHTU21DFanSwitch('Fan')
    ]
    async_add_entities(switches)


class multiHTU21DHeaterSwitch(SwitchDevice):
    """Representation of an Heater switch."""

    def __init__(self, name):
        """Initialize the Switch."""
        self._name = name
        self._state = False


    @property
    def name(self):
        """Get the name of the switch."""
        return self._name

    @property
    def is_on(self):
        """Return true if heaters is on."""
        return self._state

    async def turn_on(self, **kwargs):
        """Turn the pin to high/on."""
        self._state = True
        await self.hass.async_add_job(multiHTU21D.BOARD.turnHeater, self._state)
        # multiHTU21D.BOARD.turnHeater(self._state)

    async def turn_off(self, **kwargs):
        """Turn the pin to low/off."""
        self._state = False
        await self.hass.async_add_job(multiHTU21D.BOARD.turnHeater, self._state)


class multiHTU21DFanSwitch(SwitchDevice):
    """Representation of an Heater switch."""

    def __init__(self, name):
        """Initialize the Switch."""
        self._name = name
        self._state = False


    @property
    def name(self):
        """Get the name of the switch."""
        return self._name

    @property
    def is_on(self):
        """Return true if heaters is on."""
        return self._state

    async def turn_on(self, **kwargs):
        """Turn the pin to high/on."""
        self._state = True
        # multiHTU21D.BOARD.turnFan(self._state)
        await self.hass.async_add_job(multiHTU21D.BOARD.turnFan, self._state)

    async def turn_off(self, **kwargs):
        """Turn the pin to low/off."""
        self._state = False
        await self.hass.async_add_job(multiHTU21D.BOARD.turnFan, self._state)
