import subprocess
import tempfile
import pathlib
import networkx
import logging
import parser_json
from demangler import Demangler

logger = logging.getLogger(__name__)

def uniq(data):
    out = []
    last = None
    for x in data:
        if x == last:
            continue
        last = x
        out.append(x)
    return out

def compress(data):
    data = uniq(data)
    w = max(int(sum(map(len, data))**.5 * 1.8) + 5, 40)
    if all(len(d) <= w for d in data):
        return "\n".join(data)
    return "\n\n".join(chunk(d, w) for d in data)

def chunk(x, l):
    out = [x[i:i+l] for i in range(0, len(x), l)]
    if out and len(out) >= 2 and len(out[-1]) < 2:
        out[-2] += out[-1]
        del out[-1]
    return "\n".join(out)


class Parser(parser_json.Parser):
    def __init__(self):
        super().__init__()
        self.graph = networkx.DiGraph()
        self.graph.add_node(0)
        self.make_ptr_compactify(0)
    def add_event(self, event: dict[str, any]):
        try:
            event["from_"] = event["from"]
            del event["from"]
        except KeyError:
            pass
        super().add_event(event)
        try:
            fun = getattr(Parser, event["type"])
        except AttributeError:
            logger.warn(f"Ignoring event {event}")
            return
        del event["type"]
        try:
            fun(self, **event)
        except Exception as e:
            logger.warn(f"Exc ({e}), event={event}")
            raise

    def _demangle_symbols(self, info):
        if isinstance(info, dict):
            vld = {k:v for k, v in info.items() if isinstance(v, str) or isinstance(v, bytes)}
            rest = {k:v for k, v in info.items() if k not in vld}
            sss = list(vld.items())
            syms = Demangler.demangle(*[v for k, v in sss])
            for (k, _), v in zip(sss, syms):
                rest[k] = v
            return rest
        else:
            return info

    
    def _set_name(self, vertex, name):
        d = self._demangle_symbols(name)

        self.graph.nodes[vertex]["shape"] = "ellipse"
        self.graph.nodes[vertex]["style"] = "filled"
        self.graph.nodes[vertex]["fillcolor"] = "gray92"
        self.graph.nodes[vertex]["fontname"] = "monospace"

        colors = {
                "promise_base": "brown",
                "future_base": "turquoise4",
                "task": "dodgerblue2"
        }

        if d == "sem":
            self.graph.nodes[vertex]["fillcolor"] = "red"
            #self.graph.nodes[vertex]["fontcolor"] = "red"
            self.graph.nodes[vertex]["style"] = "filled"
            self.graph.nodes[vertex]["shape"] = "egg"

        try:
            if (x := d.get("base_type", None)):
                if x in ["future_base", "promise_base", "task"]:
                    self.graph.nodes[vertex]["fontcolor"] = colors[x]
                    del d["base_type"]
        except AttributeError:
            pass
        self.append_label(vertex, d)


    def append_label(self, vertex, label):
        v = self.graph.nodes[vertex]
        try:
            v["label"]
        except KeyError:
            v["label"] = [vertex]
        self.graph.nodes[vertex]["label"].append(label)

    def _squash_labels(self):
        for v in self.graph.nodes:
            try:
                l = self.graph.nodes[v]["label"]
            except:
                l = [str(v)]
            self.graph.nodes[v]["label"] = compress(list(map(str, l)))

    def _write_svg(self, path):
        self._squash_labels()
        networkx.drawing.nx_agraph.write_dot(self.graph, "/tmp/deadlock.dot")
        subprocess.call(["dot", "-Tsvg", "/tmp/deadlock.dot", "-o", path])

    def _add_edge(self, v1, v2, **kwargs):
        if "arrowhead" not in kwargs:
            kwargs["arrowhead"] = "box"
        self.graph.add_edge(v1, v2, **kwargs)

    def attach_func_type(self, *, func_type, vertex, file=None, line=None, **kwargs):
        vertex_ = self.make_ptr_compactify(vertex)
        demangled = str(Demangler.demangle(func_type)[0])
        if file and line:
            file = "/".join(pathlib.Path(file).parts[-2:])
            demangled = (f"{file}:{line}", demangled)
        self.append_label(vertex_, demangled)

    def vertex_ctor(self, *, vertex, **kwargs):
        self.graph.add_node(self.make_ptr_compactify(vertex))
        try:
            d = {"type": vertex["type"], "base_type": vertex["base_type"]}
            self._set_name(self.make_ptr_compactify(vertex), d)
        except KeyError:
            pass
        # TODO: add_node only after it has been added to generation counter
        # TODO: set name
    def vertex_dtor(self, *, vertex, **kwargs):
        vertex_ = self.make_ptr_compactify(vertex)
        self.graph.nodes[vertex_]["shape"] = "none"
    def vertex_move(self, *, from_, to, **kwargs):
        from__, to_ = map(self.make_ptr_compactify, [from_, to])

        # TODO merge labels
        # TODO make sure that moved and destructed object doesn't impact the
        # shape of the visualized box
        pass
    def edge(self, *, pre, post, speculative=False, **kwargs):
        pre_ = self.make_ptr_compactify(pre["address"])
        post_ = self.make_ptr_compactify(post["address"])

#        self._set_name(pre_, pre)
#        self._set_name(post_, post)
        if not speculative:
            self._add_edge(pre_, post_, arrowhead="curve")
        else:
            self._add_edge(pre_, post_, arrowhead="curve", color="purple")
    def sem_ctor(self, *, sem, **kwargs):
        sem_ = self.make_ptr_compactify(sem)
        self.graph.add_node(sem_)
        # TODO: set name, available units
    def sem_dtor(self, *, sem, **kwargs):
        sem_ = self.make_ptr_compactify(sem)
        self.graph.nodes[sem_]["shape"] = "ellipse"
    def sem_move(self, *, from_, to, **kwargs):
        from__, to_ = map(self.make_ptr_compactify, [from_, to])
        # TODO merge labels
        # TODO make sure that moved and destructed object doesn't impact the
        # shape of the visualized box
        self.graph.add_node(to_)
    def sem_wait(self, *, sem, count, pre, post, **kwargs):
        sem_ = self.make_ptr_compactify(sem)
        pre_ = self.make_ptr_compactify(pre)
        post_ = self.make_ptr_compactify(post)
        self._add_edge(pre_, sem_, label=f"wait1(count={count},post={post_})", color="limegreen", weight=2)
        self._add_edge(sem_, post_, label=f"wait2(count={count},pre={pre_})", color="limegreen", weight=2)
        try:
            self.graph.nodes[sem_]["label"]
        except:
            self._set_name(sem_, "sem")
    def sem_wait_completed(self, *, sem, post, **kwargs):
        pass
    def sem_signal(self, *, sem, count, vertex, **kwargs):
        sem_ = self.make_ptr_compactify(sem)
        vertex_ = self.make_ptr_compactify(vertex)
        # Wymyśleć coś lepszego niż name=="sem"
        try:
            self.graph.nodes[sem_]["label"]
        except:
            self._set_name(sem_, "sem")
        self._add_edge(sem_, vertex_, label="signal()", color="blue", weight=2)
