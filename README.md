# mvemu.chip8

A CHIP-8 emulator.
</br>
Not portable. Works on Linux and WSL (as long as you have a display server proxy).

## Dependencies

 - **SDL2**: graphic display & keyboard input
 - **portaudio**: sound generation

## Command line options

These are the important ones. Run with `--help` for a complete list.
 - **-a, --audio-dev**: audio device id that's used by <em>portuadio</em> as output device. If you don't specify this, you'll get a list of output devices. Look for `pulseaudio` or `pipewire`.
 - **-c, --cpu-freq**: CPU frequency in Hz. Pick something between 200-500 for a realistic experience.
 - **-i, --ref-int**: refresh the screen every N instructions. Note that frequent refreshes (e.g.: `-i 1`) is likely to cause a segfault in <em>libSDL2</em>. Aim for ~30fps.
 - **-l, --lazy-render**: refesh the screen after every screen updating instruction. Makes the previous option redundant. Whether using this is worth it or not depends on the ROM you execute.
 - **-n, --new-shift**: use the newer (Super CHIP-8) implementation of the shift operations. Required for ROMs such as space invaders.

Here are some examples of how you should run various ROMs:

```bash
$ ./bin/mvemu.chip8 -n -a 13 -i 10 -s 10 -c 300 ./roms/games/invaders.ch8
$ ./bin/mvemu.chip8 -a 13 -l -s 20 -c 500 ./roms/demos/ibm.ch8
```

## Keybinds (not remappable)


<table cellpadding="0" cellspacing="0" border="0">
    <tr>
        <td>
            <table border="1">
                <tr> <center> CHIP-8 </center> </tr>
                <tr> <td>1</td> <td>2</td> <td>3</td> <td>C</td> </tr>
                <tr> <td>4</td> <td>5</td> <td>6</td> <td>D</td> </tr>
                <tr> <td>7</td> <td>8</td> <td>9</td> <td>E</td> </tr>
                <tr> <td>A</td> <td>0</td> <td>B</td> <td>F</td> </tr>
            </table>
        </td>
        <td>
            <table border="1">
                <tr> <center> Host keyboard </center> </tr>
                <tr> <td>1</td> <td>2</td> <td>3</td> <td>4</td> </tr>
                <tr> <td>Q</td> <td>W</td> <td>E</td> <td>R</td> </tr>
                <tr> <td>A</td> <td>S</td> <td>D</td> <td>F</td> </tr>
                <tr> <td>Z</td> <td>X</td> <td>C</td> <td>V</td> </tr>
            </table>
        </td>
    </tr>
</table>

## Project structure and particularities

  - **src/cli_args.c**: definition of CLI arguments and parser. Based on `argp`.
  - **src/display.c**: sprite drawing and screen refresh. Updates are rendered to a 32x64 texture. On screen refresh, the texture is copied to the backbuffer and scaled automatically during this process.
  - **src/main.c**: emulator entry point. Not much to look at here.
  - **src/sound.c**: a sin-based audio signal generator and all the necessary setup code.
  - **src/system.c**: handles instruction decoding and interpretation. All timers are based on POSIX differential timers. If the frequency is too high (i.e.: single clock cycle time slice is too short), a whole cycle is abandoned and a warning is displayed. Detection of such cases is done by comparing the decoder function's RBP to a reference value. This works only because a POSIX timer's callback is executed in the same thread but with a separate stack (allocated once, during the timer creation). This behaviour may vary across implementations of POSIX timers, so I can't guarantee that the emulator will work.
  - **include/util.h**: just some macros that I like using for logging. Also, some other handy definitions.

## Sources for included ROMs

 - [IBM logo](https://github.com/loktar00/chip8)
 - [various games](https://www.zophar.net/pdroms/chip8/chip-8-games-pack.html)
 - [test ROM #1](https://github.com/corax89/chip8-test-rom)
 - [test ROM #2](https://github.com/metteo/chip8-test-rom)
 - [test suite](https://github.com/Timendus/chip8-test-suite)

