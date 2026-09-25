# Model-node first-write watch (diagnostic scaffold)

This probe is opt-in and changes no model data. It uses `GE_MODEL_WATCH` to
select the resource *filename* (for example `Pchrtt33Z`). The checkpoint is
placed immediately after the fresh file is loaded and the header's root is
located, **before** pointer promotion and model preprocessing. The debugger
watches the eight-byte `Data` and `Child` fields in that root node, recording
every changed value and its stack. The normal relocation is expected to be
one of the first writes. On each new load of the selected name, it re-arms
both watchpoints at the new allocation, including a same-address reload. At
stage end it retires the watchpoints before the bank's bytes are reused.

This must be built and run from the MSYS2 **MINGW64** shell on Windows with
`mingw-w64-x86_64-gdb` installed. Use the existing local ROM and generated
sidecars. Work in the project folder with the diagnostic changes applied:

```sh
./build-pc.sh ntsc-final
GE_MODEL_LIFE=1 GE_MODEL_WATCH=Pchrtt33Z GE_MP_SIM_PROBE=1 GE_MP_TEST_PLAYERS=2 \
  gdb -q -x tools_pc/model_watch.gdb --args build-pc/ge007.x86_64.exe \
  2>&1 | tee model-watch.log
```

In the game, repeat the same first-match → quit → second-match sequence. On a
crash, enter `bt 20` and `quit` at the GDB prompt. For a summary:

```sh
python3 tools_pc/model_watch_summary.py model-watch.log
```

Look for the earliest `NONZERO_HIGH` event in the **active** allocation. Its
`pc` and stack locate the writer, not merely the later render reader. A raw
node that is already bad at `RAW`, or a bad value after a checkpoint without
any matching watchpoint event, calls for a different watch target or a check
of whether GDB installed hardware watchpoints. Keep the full GDB log and the
build's exact source/binary together. To watch a body model, replace the
filename with its `Pchr...Z` resource name and repeat. The observed TT33
failure is at the root node; other failures may require another node target.

This is a first-write investigation, not a proposed cache fix. The ordinary
`GE_MODEL_LIFE` log compares persistent headers and stage generations; it
cannot validate the inner `ModelNode` fields by itself. No watchpoint is armed
without `GE_MODEL_WATCH`. Running without GDB still prints the three
`[MODEL-WATCH]` checkpoints for the selected model.
