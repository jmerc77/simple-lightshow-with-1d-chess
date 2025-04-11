# simple-lightshow-with-1d-chess
An arduino lightshow for Christmas/4th of July/etc. Using an addressable LED strip that also has a playable 1D chess game via an optical wand. Only 2 I/O pins needed (one is an analog pin).
# File Contents
## pin layout.txt
Basic description of the circuitry.

**May not be readable in all editors due to ASCII art for the diagrams.**

## Calibration.txt
Instructions on how to calibrate the optical wand.

## chess.pdf
Basic instructions on how to interpet the game board and wand usage.

## xmas_leds
The folder for the code. The arduino ino file inside contains all of my code. One libraray is required (Adafruit_NeoPixel).

# Usage
Plug a 5V adaptor with at least 2A output (and flip the power switch if added) for power. To start a 1D chess game, press the button on the wand. Play the game as described in chess.pdf and when the game finishes, the strip returns to decorations. If the wand is "glitching" by flashing too many white LED sequences, see calibration.txt to re-calibrate the wand. It is helpfull to read in advanced of course.
