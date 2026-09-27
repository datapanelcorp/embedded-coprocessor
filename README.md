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
development environment. For example, `--conn-modifiers sn=066DFF5355507551870355194216`.

## Simulation

The firmware also builds for Zephyr's `native_sim` target, which runs on the host. The
ports are replaced by fakes (`dp,port-fake`) and ECP requests use the simulator backend
instead of the UART. See `boards/native_sim.overlay`.

`west build -p always -b native_sim apps/embedded-coprocessor`

## Testing

Tests for the ECP command handlers are in `tests/`. They run on `native_sim`, together
with the dpunity tests:

`west dp-test --sanitize`

To run only this application's tests:

`west twister -p native_sim -T apps/embedded-coprocessor/tests`

- `tests/commands` sends requests through the ECP device command handler, as the host
  would, and checks the responses. The ports are fakes, so tests control what the port
  driver returns and check how it was called. Because ENUM can't be undone, the suites
  run in phases: unconfigured, ENUM, active, then ESTOP. Scenarios in `testcase.yaml`
  cover 13A enumeration, LED support, and optional features disabled.
- `tests/reboot` covers REBOOT and BOOT_JUMP, which never return, with one scenario each.
- `tests/host` runs the ECP host driver, as used by the main MCU, against this
  application over a pair of emulated UARTs. It can corrupt bytes on the "wire" to test
  error recovery.
- `tests/fuzz` is a libFuzzer target for the device command handler and this
  application's commands. See below.

### Fuzzing

The fuzz target sends random sequences of requests to the ECP, and reports a crash if
the ECP fails to respond with a correctly framed response, or if a sanitizer or
assertion fails. It needs clang. From the top-level zephyr project directory:

```
ZEPHYR_TOOLCHAIN_VARIANT=llvm west build -b native_sim -d build-fuzz apps/embedded-coprocessor/tests/fuzz
python3 apps/embedded-coprocessor/tests/fuzz/make_seeds.py fuzz-seeds
mkdir -p fuzz-corpus
build-fuzz/zephyr/zephyr.exe -max_total_time=300 -dict=apps/embedded-coprocessor/tests/fuzz/ecp.dict fuzz-corpus fuzz-seeds
```

libFuzzer adds interesting inputs to `fuzz-corpus`, so later runs continue from where
earlier ones left off. When it finds a crash, it saves the input as `crash-<hash>`; run
`build-fuzz/zephyr/zephyr.exe crash-<hash>` to reproduce it.

PEEK and POKE are disabled in the fuzz target, because on `native_sim` they accept any
address. ENUM, REBOOT and BOOT_JUMP are sent only once, at startup, or not at all.

### Known deviations from the specification

Tests for behavior the ECP protocol specification requires, but the firmware doesn't
yet implement, are skipped. Each prints `Known deviation from spec` with the reason.
To run them, pass `-x CONFIG_TEST_ECP_KNOWN_DEVIATIONS=y` to twister. Some deviations
reboot the ECP, which ends the test run, so run those individually with the
executable's `-test=<suite>::<test>` option.

Also see `ztest/README.md` in the top-level repo.
