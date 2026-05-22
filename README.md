# Neo Liminal

This is a little fun project I wanted to test : vibe coding an audio effect.

It unites every kind of effects I love in one simple plugin. Each knob has one effect from 1% of the knob to 50%, and from 51% to 100% another effect applies on top. Here's the effect list :

- Feedback knob : delay / slowdown
- Reverb knob : plate reverb + shimmer down / shimmer up
- Tape knob : vibrato / wow + flutter
- Dataloss knob : saturation / drops

## Install

Download the latest release from the [Releases page](../../releases/latest).

### macOS

**VST3** : unzip and copy `NeoLiminal.vst3` to `~/Library/Audio/Plug-Ins/VST3/`

**AUv3** : unzip and copy `NeoLiminal.app` to `/Applications/`

> The plugin is not notarized. If macOS Gatekeeper blocks it, run this in Terminal:
> ```
> xattr -cr /Applications/NeoLiminal.app
> ```

### Windows

**VST3** : unzip and copy `NeoLiminal.vst3` to `C:\Program Files\Common Files\VST3\`

## TODO

- support iOS/iPad
  - UI + screen sizes + touch events
  - publish on App Store
  - Apple Developer certificate
  - so : we will need to commercialize it
  - so : optimizations
  - so : documentation
  - so : website
  - so : videos

## License

This is released under the license [Don't Be A Dick](https://dont-be-a-dick.animi.st/).
