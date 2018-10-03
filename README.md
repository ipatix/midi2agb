# midi2agb
This is a reimplementation of Nintendo's mid2agb tool. It's not related to the original and is intended to fix a bunch of mid2agb's flaws.
The state of bugs is now "reasonably stable". I did spend quite a bit of time on testing. If you still encounter any problems, please open up an issue or send me an e-mail.

### These are the following things that it improves over the original mid2agb:

* non proprietary software
* auto optimizes the input MIDI to remove unnecessary events (removes trailing waits or waits with no events inbetween)
* parses standard MIDI parameters correctly, like pitch bend range or expression
* allows for various global song parameters to be set via meta events (like a modulation scale which often scales differently in MIDI software)
* can apply a natural volume scale so the loudness has the same scale as your MIDI software

### TODO:

* The Pattern Detection currently works, but patterns aren't always used because the byte count comparison doesn't take the byte reduction by running state into account. The worst case of this is a song which is a couple of bytes larger than it should be.
* Support weird AGB events like "memacc" or "xcmd".

### Usage:

```
midi2agb [options] <input.mid> [<output.s>]
```

Option | Parameter | Default | Description
--- | --- | --- | ---
-s | symbol | file name | symbol name for the song header (address for the linker)
-m | volume | 128 | master movule for the song (0..128), 128=original
-g | voicegroup | voicegroup000 | the default tone color set (aka voicegroup or soundbank)
-p | priority | 0 | priority of song for the music engine (0..127)
-r | reverb | 0 | enables song reverb if > 0 (0..127)
-n | *-* | disabled | enables natural volume scale to approximate MIDI like loudness
-v | *-* | disabled | enables debug output
--modt | value | 0 | 0 = pitch modulation, 1 = volume modulation, 2 = panpot modulation
--modsc | value | 1.0 | scale the song's modulation by factor
--lfos | value | 22 | modulation speed: (value * 24 / 256) oscillations per beat
--lfodl | value | 0 | modulation delay after start of a note


### Binaries / Compiling:

The binaries in the "Releases" section might not be up to date. It's highly recommended to use the latest version from source for the latest bug fixes.
When compiling from source, you'll also need cppmidi which is a git subrepo. Type "git submodule update --init" when trying to compile and it can't find cppmidi.

### License

This tool is licensed under the MIT license. See the LICENSE file for details.