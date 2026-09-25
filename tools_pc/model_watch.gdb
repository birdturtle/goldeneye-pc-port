set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
python
import gdb

_current = {"node": 0, "generation": 0, "watches": [], "writes": 0}


def _number(expr):
    return int(gdb.parse_and_eval(expr))


def _value(address):
    return _number("*(unsigned long long *)0x%x" % address) & 0xffffffffffffffff


class NodeWrite(gdb.Breakpoint):
    def __init__(self, node, offset, field, generation):
        self.address = node + offset
        self.field = field
        self.generation = generation
        self.before = _value(self.address)
        super().__init__("*(unsigned long long *)0x%x" % self.address,
                         type=gdb.BP_WATCHPOINT, wp_class=gdb.WP_WRITE,
                         internal=True)

    def stop(self):
        try:
            now = _value(self.address)
            old = self.before
            self.before = now
            _current["writes"] += 1
            high = now >> 32
            label = "NONZERO_HIGH" if high else "WRITE"
            pc = gdb.selected_frame().pc()
            gdb.write("[MODEL-WATCH-GDB] %s gen=%d node=0x%x field=%s "
                      "old=0x%016x new=0x%016x pc=0x%x write=%d\n" %
                      (label, self.generation, self.address -
                       (8 if self.field == "Data" else 40), self.field,
                       old, now, pc, _current["writes"]))
            try:
                gdb.write(gdb.execute("bt 10", to_string=True))
            except Exception as error:
                gdb.write("[MODEL-WATCH-GDB] STACK_UNAVAILABLE %s\n" %
                          error.__class__.__name__)
        except Exception as error:
            gdb.write("[MODEL-WATCH-GDB] WATCH_ERROR %s\n" % error)
        return False


class Checkpoint(gdb.Breakpoint):
    def __init__(self):
        super().__init__("modelLifeWatchBreak", internal=False)

    def stop(self):
        try:
            node = _number("g_ModelWatchNode")
            generation = _number("g_ModelWatchGeneration")
            phase = gdb.parse_and_eval("g_ModelWatchPhase").string()
            if phase == "RETIRED":
                for watch in _current["watches"]:
                    watch.delete()
                _current["watches"] = []
                gdb.write("[MODEL-WATCH-GDB] RETIRED gen=%d node=0x%x\n" %
                          (generation, node))
                return False
            gdb.write("[MODEL-WATCH-GDB] CHECKPOINT gen=%d phase=%s node=0x%x "
                      "Data=0x%016x Child=0x%016x\n" %
                      (generation, phase, node, _value(node + 8),
                       _value(node + 40)))
            if phase == "RAW":
                for watch in _current["watches"]:
                    watch.delete()
                _current["watches"] = []
                _current["node"] = node
                _current["generation"] = generation
                _current["watches"] = [
                    NodeWrite(node, 8, "Data", generation),
                    NodeWrite(node, 40, "Child", generation)]
                gdb.write("[MODEL-WATCH-GDB] ARM gen=%d node=0x%x\n" %
                          (generation, node))
        except Exception as error:
            gdb.write("[MODEL-WATCH-GDB] CHECKPOINT_ERROR %s\n" % error)
        return False


Checkpoint()
gdb.write("[MODEL-WATCH-GDB] ready; expecting GE_MODEL_WATCH=<resource name>\n")
end
run
