This is the Embedded Co-Processor firmware.

# Development tasks

## Building an application

The tasks below assume you have already set up a development environment for Zephyr.

Also see the top-level repo's README.

## Updating

1. `west update`

## Compiling

From the top-level zephyr project directory:

`west build -p always -b datapanel_45116@A/stm32g051xx apps/embedded-coprocessor`

This builds the coprocessor firmware.

TODO: describe how coprocessor firmware gets incorporated into the block's firmware update.

## Flashing

1. `west flash`

You may need to specify additional options depending on your
development environment. For example, `--esp-device /dev/ttyUSB0`.
