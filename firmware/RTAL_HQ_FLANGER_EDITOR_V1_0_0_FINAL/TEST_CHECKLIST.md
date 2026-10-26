# RC4 final acceptance checklist

Record Windows version, Python version, firmware boot version, editor build, and test date.

- [ ] Startup: VISUAL DSP opens; window fits screen; branding reads RealTimeAudioLab.
- [ ] MIDI IN and OUT selectable; restart automatically reconnects.
- [ ] Unplug/replug MIDI: editor does not crash; reconnect succeeds.
- [ ] Bidirectional parameter changes and device/editor values agree.
- [ ] Preset 001, 050, 128: load, edit, save, rename, restart and verify.
- [ ] Last used preset restored after device power cycle.
- [ ] Morph read/apply/save persists after power cycle.
- [ ] Morph PLAY, STOP, REVERSE, ping-pong and start direction.
- [ ] Background fetch A=001 B=105 while 050 active: audible preset unchanged.
- [ ] Morph preview graph responds to reported position.
- [ ] FREEZE stops graphics fully; unfreeze resumes; audio unaffected.
- [ ] A/B compare, PNG export, SVG export and quality/FPS controls.
- [ ] Window size and preferences persist after editor restart.
- [ ] Build EXE on Windows; launch on a clean Windows 10/11 machine.
- [ ] Run 60-minute MIDI/visualization soak test; note exceptions and CPU load.
