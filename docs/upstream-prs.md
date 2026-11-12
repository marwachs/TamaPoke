# Upstream PR drafts for socquique/TamaPoke

These texts are for upstream. They have **not** been opened, pushed, or commented on `socquique/TamaPoke`.

Base is current upstream `main`, `00136942f42c651ceb75fbfef8499a2df945cde7` ("Volume levels: sound button cycles high, medium, low, off (#21)"). Fetched during this draft. It has not moved, so no branch needed a rebase onto newer commits, and the host tests and 1.75C image were not rebuilt for a moving base. Each branch below is a descendant of that commit (`git rebase upstream/main` reports already up to date).

Histories that had fixup commits were squashed. The resulting trees match the previous tips, except `fix/axp-dcdc5-off`, which now calls `disableDC5()` only when `BOARD_AMOLED_175C` is set.

`integration/mawa-175c-v2` on `mawa85/TamaPoke` is the device image. It is not an upstream PR.

## Recommended merge order

Land them in this order. The first eight do not depend on each other. The last three share the clock screen, so they conflict if they land in parallel.

| Order | mawa85 PR | Branch | Why this slot |
|---|---|---|---|
| 1 | #8 | `fix/175c-pwr-key` | 1.75C wake key. Touch-lock on that board needs it. |
| 2 | #9 | `feat/battery-charge-profile` | Charger registers. Do this before DCDC5 so `batBegin()` is the new function. |
| 3 | #16 | `fix/qmi8658-low-power` | 1.75C only. No shared logic. |
| 4 | #17 | `fix/axp-dcdc5-off` | One call inside `batBegin()`. Conflicts with #9 if #9 is already merged; keep the charge setup and add the gated `disableDC5()`. |
| 5 | #13 | `feat/codec-powerdown-on-mute` | Codec register only. Parent of #15. |
| 6 | #15 | `fix/audio-clock-gating` | Contains the #13 commit. Review the second commit, or merge #13 first. |
| 7 | #12 | `feat/cpu-freq-scaling` | `power.h` only. Does not need the timeout. |
| 8 | #18 | `fix/co5300-panel-sleep` | Uses the `screenOff` flag that already exists. |
| 9 | #10 | `feat/battery-percent-option` | Adds a clock pill and moves OK. |
| 10 | #11 | `feat/screen-off-timeout` | Same clock row as #10. Place the pills side by side. |
| 11 | #19 | `feat/auto-touch-lock` | Contains the #11 commit. On the 1.75C, also needs #8, because GPIO3 is not the key. |

## Risky for the original 1.75

Tested hardware is the Waveshare ESP32-S3-Touch-AMOLED-1.75C on USB. The original 1.75 was not on the bench.

- **#9 changes the charger on both boards.** 400 mA constant current (reset is 300 mA) and a 900 mA VBUS limit (reset is 1500 mA; we had briefly used 500 mA). A 500 mA input limit plus a 400 mA target held the 1.75C in input-current regulation, which the datasheet says disables termination. 900 mA lets a full cell finish. On a port that can only supply 500 mA the PMU is now *allowed* to draw more than that port can give. Precharge is left at the 125 mA reset. `begin()` already takes the TS pin out of battery-temperature mode; this PR says so again. The button-cell charger is forced off. The 1.75C has no backup cell. Confirm the original 1.75 has none too.
- **#12 and #18 change both boards.** Screen-off at 80 MHz, including on USB, and a real CO5300 sleep (not brightness 0). Light sleep stays off while USB is connected, because the CDC port drops and `CONFIG_PM_ENABLE` is not set. The wake sequence was checked on the 1.75C, which uses the same CO5300. If that sequence is wrong on the original panel, a PWR wake can leave it black until reboot.
- **#13 and #15 change both boards.** Muting clears ES8311 `0x0D`. Idle also calls `i2s.end()`, which stops MCLK on GPIO 16. The next effect starts the clocks again before the amp. A path that played audio without going through that update would stay silent until the next one.
- **#11 and #19 change both boards.** The timeout replaces the fixed 90 s dim / 5 min near-off unless the setting is "never" (`offMs == 0` keeps the old schedule, so the existing host tests still pass). Touch-lock calls `TouchDrvCST92xx::sleep()`, which puts the IRQ pin in open-drain. Wake re-runs `begin` / reset. Both boards use the CST9217 in this sketch. On the 1.75C a short PWR press only exists after #8.
- **#8, #16 and #17 are 1.75C-only in effect.** #8 still shares `pwrShortPressed()` with the original board: both already consumed the latched PKEY short IRQ. The new code enables that IRQ on the 1.75C and stops reading GPIO3. #16 is inside `#if BOARD_AMOLED_175C`. #17 is too, after the gate added for this draft. The original 1.75 rail map is different (panel on BLDO1, not DCDC1) and must not lose DCDC5.
- **Not in any of these PRs:** the integration-only line that points `cpuMhzFor` at `dimStageNow`. On #12 alone the stages are still the historical 90 s dim and 5 min near-off. A 60 s timeout dims the panel at 30 s, but the CPU stays at the historical stage until screen-off (80 MHz) or those old times. That is consistent with each PR on its own.

## #8 `fix/175c-pwr-key`

**Title:** Read the 1.75C PWR key from the AXP2101 PKEY IRQ

**Depends on:** nothing.

**Board scope:** behavior change is 1.75C only. The original board already used the PKEY short IRQ.

### Body

Key2 on the ESP32-S3-Touch-AMOLED-1.75C is the AXP2101 PWRON pin. GPIO3 is the net SYS_OUT. It does not follow the key, so the triple-press detector never saw a press.

`pwrSetup()` keeps register `0x27` bits [3:2] at 00 (4 s hardware power-off) and enables `XPOWERS_AXP2101_PKEY_SHORT_IRQ`. The IRQ pin is not wired to an ESP32 GPIO, so the loop polls it over I2C and clears it. One short press toggles the screen once 350 ms have passed with no further press. Three shorts inside that window still lock the screen. A 4 s hold is still the hardware power-off. GPIO3 is left as an input and is not the key.

On the bench, a short press logged `PWR short` while `gpio3=0`. The previous GPIO3 reader never reached that line.

Host tests cover the 350 ms gap: one press toggles, three lock. `./test/run_tests.sh`.

## #9 `feat/battery-charge-profile`

**Title:** Let the AXP2101 finish a 4.20 V / 400 mA LiPo charge

**Depends on:** nothing. Merge before #17.

**Board scope:** both boards. The charge-done log below is from a 1.75C.

### Body

`batBegin()` programs the AXP2101 once. The loop only reads it.

- Charge enabled, CV **4.20 V** (reg `0x64` = `0x03`, the reset value).
- Constant current **400 mA** (reg `0x62` = `0x0A`). Reset is 300 mA (`0x09`).
- Termination **125 mA** (reg `0x63` = `0x15`, bit 4 set). That is the reset value. An earlier 25 mA write (`0x63` = `0x11`) never crossed the taper, so STATUS2 stayed in the charging state at 4.20 V and `isCharging()` stayed true.
- USB input limit **900 mA** (reg `0x16` = 2). At 500 mA a 400 mA target holds input-current regulation, and the datasheet disables termination for as long as that regulation is active. Reset of `0x16` is 1500 mA; upstream does not write it.
- Precharge is left at 125 mA (reg `0x61` = `0x05`). `setPrechargeCurr()` only writes bits [1:0] of that 4-bit field, so a "50 mA" call stores 150 mA. Precharge only applies below 3.0 V. This PR does not call it.
- TS pin forced out of battery-temperature mode (`disableTSPinMeasure`, reg `0x50` bit 4). The 1.75C has no NTC. `begin()` already does this.
- Button-cell charger forced off (reg `0x18` bit 2). The cell-charge enable is bit 1 of the same register and is set. There is no backup cell on the 1.75C.

Recharge is fixed in the chip at 100 mV under the CV target (4.10 V). There is no public battery-current ADC. `isCharging()` is STATUS2[7:5] == 1, which stays set through constant-voltage and clears at done.

`HEALTH` adds `st=` (tri/pre/cc/cv/done/stop), `dir=`, die temp, the raw ADC count, VBUS, the programmed ICC and ITERM, button-cell and thermal-regulation flags, which rails are on, and CPU MHz. The Celsius figure is the XPowers fit, `22 + (7274 - raw) / 20`. The datasheet only defines that channel as 0.1 mV per count, 0–0.8192 V. The line stays every 5 minutes. With the debug flag left at its default it prints every 60 s and adds FreeRTOS run-time stats (Arduino-ESP32 3.3.10 builds those stats in). `-DTAMAPOKE_CHG_DEBUG=0` restores the plain 5-minute line.

On the 1.75C, before this fix, HEALTH sat at `chg=1` with `mv=4207` and `bat=100%`. After `0x63=0x15` and `0x16=2` it reported `chg=0 st=done dir=stdby mv=4192 bat=100%`.

Low-battery save-and-shutdown (smoothed voltage under 3.30 V for 30 s, PMU cutoff 3.10 V) is already upstream and is unchanged. DCDC and LDO voltages are not written here. Turning the unconnected DCDC5 off is #17.

## #10 `feat/battery-percent-option`

**Title:** Add a setting to show battery percent instead of the icon

**Depends on:** nothing. Merge before #11; they edit the same clock row.

**Board scope:** both boards. UI only.

### Body

The clock screen gains a pill that switches the header between the existing battery icon (the default) and a number such as `87%`, with a `+` while charging. The choice is stored in NVS (`bpct`) and defaults to the icon.

No charger or power-path change.

Host tests are unchanged; this is drawing and a saved bool. The 1.75C integration image shows the pill beside the timeout pill.

## #11 `feat/screen-off-timeout`

**Title:** Add a configurable screen-off timeout

**Depends on:** nothing. #19 is based on this commit. The clock pill collides with #10.

**Board scope:** both boards.

### Body

Upstream dims at 90 s and reaches the near-off step at 5 min, on a fixed schedule, and several screens never counted as idle because they do not redraw. This adds a clock pill: **60 s** (default), 30 s, 15 s, or never. The panel dims at half that time and `screenOff` becomes true at the end, including while the clock, keyboard or gallery is open. A minigame is left running. USB does not disable the timeout. USB only skips light sleep, which it already did.

"Never" stores `offMs = 0` and keeps the 90 s / 5 min schedule, so the existing power tests still pass. The choice is NVS `off`.

`./test/run_tests.sh` covers the half-time dim, the deadline, and the `offMs == 0` legacy path.

This sets `screenOff`. It does not by itself send CO5300 sleep-in. Brightness 0 leaves the OLED scanning. That sleep sequence is #18.

## #12 `feat/cpu-freq-scaling`

**Title:** Scale the CPU down when the panel is dim or off

**Depends on:** nothing. It does not use the timeout's `offMs`.

**Board scope:** both boards.

### Body

Upstream leaves the CPU at 240 MHz and refuses light sleep whenever USB is present, so a plugged-in board with the screen off still runs the loop at 240 MHz.

`cpuMhzFor()` in `power.h`:

- **240 MHz** while the pet is in use, on USB with the screen on, or in a minigame.
- **160 MHz** once the panel has dimmed or the pet is asleep, on battery.
- **80 MHz** with the screen off or at the near-off step, **including on USB**.

The clock is not changed while a sound is playing, because the I2S bit clock follows the CPU. There is no new settings control.

Light sleep stays off while USB is connected. A light sleep drops the USB CDC port, and `CONFIG_PM_ENABLE` is not set in this core, so automatic light sleep is not available. With the screen off the loop waits 100 ms. It does not busy-spin.

The stages here are the historical 90 s dim and 5 min near-off. They do not follow a custom timeout. Screen-off itself is 80 MHz either way.

Host tests cover each step, including 80 MHz on USB with the screen off and the 100 ms wait.

On the 1.75C the board was hot on USB with the screen off at 240 MHz. 80 MHz is the clock the battery path already used in that state. The later drop in PMU die temperature (67.8 °C to 47 °C) also included the panel sleep, the I2S stop, the QMI power-down and DCDC5 off. This PR is only the clock.

## #13 `feat/codec-powerdown-on-mute`

**Title:** Power down the ES8311 while sound is muted

**Depends on:** nothing. Parent of #15.

**Board scope:** both boards. Both have the ES8311 and the NS4150B amp on GPIO 46.

### Body

The speaker amp was already forced low when sound is off. The codec stayed powered. While sound is muted this writes ES8311 `0x0D = 0x00`. Turning sound back on restores `0x0D = 0xFA` and `0x44 = 0x58`, the values from init. Idle amp gating is unchanged. I2S is left running; stopping MCLK is #15.

No new setting. The existing mute control is the switch.

## #15 `fix/audio-clock-gating`

**Title:** Stop I2S and the codec while the screen is idle

**Depends on:** #13. This branch contains that commit. The new commit is the I2S stop. Merge #13 first, or review only the second commit.

**Board scope:** both boards.

### Body

`i2s.begin()` runs from boot and `i2s.setPins` drives MCLK on GPIO 16, even when the amp is already low and nothing is playing. With the screen off and the amp not held for a sound effect, this calls `i2s.end()` and writes ES8311 `0x0D = 0x00`. The next effect sets the amp hold, starts I2S, and restores the codec before the sample plays.

#13 already clears `0x0D` while muted. This PR is the idle case, including when sound is enabled but nothing is playing, and it is what releases MCLK and BCLK.

On the 1.75C, after this and the other idle cuts, HEALTH showed `pa=0 i2s=0` and the reported die temperature fell from 67.8 °C to 47 °C and was still falling. The 67.8 °C figure is the XPowers fit (`22 + (7274 - raw) / 20`); raw was about 6358, which is 636 mV on a channel the datasheet specifies only as 0.1 mV per count. The drop is real. It is not a measurement of this PR alone.

## #16 `fix/qmi8658-low-power`

**Title:** Power down the unused QMI8658 on the 1.75C

**Depends on:** nothing.

**Board scope:** 1.75C only, inside `#if BOARD_AMOLED_175C`. The original 1.75 is unchanged.

### Body

The 1.75C QMI8658 sits on VCC3V3. This firmware never reads it. After `Wire.begin()`, the sketch probes `0x6B` then `0x6A`. If `WHO_AM_I` (`0x00`) is `0x05`, it writes `CTRL7` (`0x08`) = `0x00` (accelerometer and gyroscope off) and `CTRL1` (`0x02`) = `0x01` (`SensorDisable`, internal oscillator off, bit 0 in the SensorLib map).

Boot logs `QMI8658 0x6B power-down CTRL7=0 CTRL1=0x01` (or `0x6A`). This was one of the idle cuts on the board whose die temperature fell from 67.8 °C toward 47 °C. It is not an isolated current measurement.

## #17 `fix/axp-dcdc5-off`

**Title:** Turn off the unconnected AXP2101 DCDC5 on the 1.75C

**Depends on:** nothing logically. Textually it conflicts with #9, because both edit `batBegin()`. Merge #9 first and keep `disableDC5()` at the end of that function.

**Board scope:** 1.75C only. The call is inside `#if BOARD_AMOLED_175C`.

### Body

On the 1.75C schematic DCDC5 has no inductor and its output capacitor is not fitted. `batBegin()` calls `pmu.disableDC5()` only in the 1.75C build. DCDC1 stays on: it is VCC3V3 for the ESP32, PSRAM, USB and the panel. DCDC2 (0.9 V), DCDC3 (1.2 V), DCDC4 (1.8 V) and the ALDOs are labeled as used and are not written.

The original 1.75 powers the OLED from BLDO1 and uses a different rail map. This PR does not call `disableDC5()` in that build.

No before/after current for this converter alone. It was one of the idle cuts on the 1.75C whose die temperature fell from 67.8 °C toward 47 °C. `HEALTH` on #9 prints a rail bitmask; DCDC5 should read off on a 1.75C after boot.

## #18 `fix/co5300-panel-sleep`

**Title:** Put the CO5300 to sleep when the screen turns off

**Depends on:** nothing. `screenOff` already exists (PWR toggle). The timeout in #11 is another way to set that flag.

**Board scope:** both boards. The 1.75C lock blank uses it too. Confirmed visually on a 1.75C. Same CO5300 on the original 1.75.

### Body

Writing brightness 0 (reg `0x51`) leaves the CO5300 scanning, so the panel stays lit. On the 1.75C the log already said the screen was off (`off=1`, idle past the timeout) while the panel was visibly on, and the PMU stayed hot.

Screen-off, and the 1.75C lock blank, now send:

1. reg `0x51` = 0
2. display off `0x28`
3. wait 120 ms
4. sleep-in `0x10`
5. wait 120 ms

Wake is sleep-out `0x11`, wait 120 ms, display on `0x29`, then `0x51` = brightness. The GFX `displayOn()` sends `0x29` before `0x11` and is not used. Commands wait until the previous QSPI frame is done. The sequence is sent once per edge.

DCDC1 is not switched off. On the 1.75C it is VCC3V3 for the ESP32 as well as the panel. On the original 1.75 the panel rail is BLDO1, which this PR also does not touch.

Host tests check the command order and that a repeated call does not send it again. The log on the device shows `panel cmd 0x28` then `panel cmd 0x10`, and the panel goes dark.

## #19 `feat/auto-touch-lock`

**Title:** Lock touch after the screen has been off

**Depends on:** #11. This branch contains that commit. The touch-lock commit is the second one. On the 1.75C, waking with the big PWR key also needs #8. This branch clears the lock whenever `screenOff` goes from true back to false, so whichever wake the board already has also unlocks touch.

**Board scope:** both boards. Both use the CST9217. The 1.75C PWR key does not work without #8.

### Body

After the screen has been off for a saved delay, touches no longer wake it and no longer register. A short PWR press wakes the screen and clears the lock in the same step. Until the delay elapses, touch still wakes as it does today.

The clock pill cycles off, 0 s, 10 s, 30 s and 60 s. Default is 10 s, NVS byte `tlk` (0 = 10 s, 1 = 30 s, 2 = 60 s, 3 = 0 s, 4 = off). The label is `L0` / `L10` / `L30` / `L60`, or the word off. English is `off`, German is `aus`. The other languages use `off` so the string stays a single byte. The triple-press pocket lock is unchanged: its unlock is still three presses, not one.

The CST9217 is put to sleep only once the lock engages (`touch.sleep()`). During the delay it stays awake, so a touch can still wake the screen. `sleep()` leaves the IRQ pin open-drain, and reading a sleeping CST9217 hangs for about a second, so `handleTouch` returns before any I2C while the lock is held. The PWR wake re-runs `begin`, reset, coordinates and the falling-edge interrupt. That full re-init is the wake path. There is no separate unlock gesture.

Serial lines are `touch lock` and `touch unlock`. `HEALTH` adds `tlock=0/1`.

Host tests cover the 10 s delay, lock at 0 s, the off setting, a wake that cancels the timer, millis wrap, and the NVS selector. `./test/run_tests.sh` also checks that every language has the new string.

Place the pill to the left of the timeout pill from #11 and the percent pill from #10. On a 466-wide clock those three fit at x=36, x=133 and x=243, with OK still on the row below.
