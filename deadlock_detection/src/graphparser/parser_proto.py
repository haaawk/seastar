from typing import Any
from itertools import chain
import logging

from proto.deadlock_trace_pb2 import deadlock_trace
from generation_counter import GenerationCounter, Vertex
import json

"""
Data format on the output. There are JSONs on each line.
Each line describes an event. "type" field indicates the event. Each event has a timestamp, expressed in nanoseconds. Identifiers of semaphores and vertices are small integers.

Syntax:
    event="sem_ctor", sem=unique_id, count=initial_units
    event="sem_dtor", sem=unique_id
    event="vertex_ctor", vertex=unique_id
    event="vertex_dtor", vertex=unique_id
    event="sem_wait", sem=unique_id, pre=unique_id of vertex taking, post=unique_id of vertex receiving, count=units taken
    event="sem_wait_completed", sem=unique_id, post=unique_id of vertex receiving, count=units taken
    event="sem_signal", sem=unique_id, vertex=unique_id of signalling vertex, count=units returned
    event="edge", pre=unique_id, post=unique_id, speculative="0" or "1"
"""

logger = logging.getLogger(__name__)

class Compactify:
    def __init__(self):
        self.seen = {}
    def add(self, vertex):
        try:
            return self.seen[vertex]
        except KeyError:
            new_id = len(self.seen)
            self.seen[vertex] = new_id
            return new_id
    def add_move(self, v1, v2):
        k = self.seen[v2] = self.add(v1)
        return k

def proto_to_dict(proto: deadlock_trace) -> dict[str, Any]:
    def conv_v(vertex):
        return {"address": vertex.address, "type": vertex.type_id}
    obj = {
            deadlock_trace.EDGE: {"pre": conv_v(proto.pre), "post": conv_v(proto.vertex), "speculative": bool(proto.value)},
            deadlock_trace.VERTEX_CTOR: {"vertex": conv_v(proto.vertex)},
            deadlock_trace.VERTEX_DTOR: {"vertex": conv_v(proto.vertex)},
            deadlock_trace.VERTEX_MOVE: {"from_": conv_v(proto.pre), "to": conv_v(proto.vertex)},
            deadlock_trace.SEM_CTOR: {"sem": {"address": proto.sem, "available_units": proto.value}},
            deadlock_trace.SEM_DTOR: {"sem": {"address": proto.sem, "available_units": proto.value}},
            deadlock_trace.SEM_MOVE: {"from_": {"address": proto.pre.address}, "to": {"address": proto.sem}},
            deadlock_trace.STRING_ID: {"type_id": proto.value, "name": proto.extra},
            deadlock_trace.FUNC_TYPE: {"vertex": conv_v(proto.vertex), "type_id": proto.value, "file_line": proto.extra},
            deadlock_trace.SEM_SIGNAL: {"sem": {"address": proto.sem}, "count": proto.value, "vertex": conv_v(proto.vertex)},
            deadlock_trace.SEM_WAIT: {"sem": {"address": proto.sem}, "count": proto.value, "pre": conv_v(proto.pre), "post": conv_v(proto.vertex)},
            deadlock_trace.SEM_WAIT_CMPL: {"sem": {"address": proto.sem}, "post": conv_v(proto.vertex)},
    }[proto.type]
    obj["timestamp"] = proto.timestamp
    return obj

class Parser:
    def __init__(self):
        self.gc = GenerationCounter()
        self.compactify = Compactify()
        self._log = []
        self.started_waits = dict()
        self.moved_vertices = set()

    def dump_vertex_matching(self):
        return dict(self.compactify.seen.items())

    def log(self, type_: str, timestamp: int, obj):
        obj["type"] = type_
        obj["timestamp"] = timestamp
        self._log.append(obj)

    def load_events_from_file(self, file: str):
        out = []
        with open(file, "rb") as f:
            while True:
                r = f.read(2)
                if len(r) < 2:
                    break
                size = int.from_bytes(r, "little")
                msg = f.read(size)
                if len(msg) < size:
                    break
                pb = deadlock_trace.FromString(msg)
                out.append(pb)
        return out

    #def add_files(self, file_contents: list['file']):
    def add_files(self, file_contents: list[str]):
        objects = list(chain.from_iterable(map(self.load_events_from_file, file_contents)))
        objects.sort(key=lambda event: event.timestamp)
        start_time = objects[0].timestamp
        for obj in objects:
            obj.timestamp -= start_time
            self.add_event(obj)
    def output(self, file_name: str):
        with open(file_name, "w") as f:
            for line in self._log:
                print(json.dumps(line), file=f)
    def add_event(self, event: deadlock_trace):
        type = deadlock_trace.event_type.Name(event.type).lower()
        ev = proto_to_dict(event)
        try:
            fun = getattr(Parser, type)
        except AttributeError:
            logger.warn(f"Ignoring event from regular parser: {event}")
            ev["type"] = type
            return ev

        try:
            fun(self, **ev)
        except Exception as e:
            print(e, ev)
            raise
        ev["type"] = type
        return ev

    def make_ptr(self, ptr):
        try:
            addr = ptr["address"]
        except TypeError:
            addr = ptr
        return self.gc.get_vertex(addr)

    def make_ptr_compactify(self, ptr):
        return self.compactify.add(self.make_ptr(ptr))

    def sem_ctor(self, *, sem, timestamp, **kwargs):
        sem_ = self.gc.add_vertex(sem["address"])
        self.log("sem_ctor", timestamp, dict(
            sem=self.compactify.add(sem_),
            count=sem["available_units"], 
        ))
    def sem_dtor(self, *, sem, timestamp, **kwargs):
        if kwargs:
            logger.warn(f"sem_dtor got sem={sem}, kwargs={kwargs}")
        sem_ = self.gc.del_vertex(sem["address"])
        sem__ = self.compactify.add(sem_)
        if sem_ not in self.moved_vertices:
            self.log("sem_dtor", timestamp, dict(
                sem=sem__,
            ))
    def sem_move(self, *, from_, to, timestamp):
        to_ = self.gc.add_vertex(to["address"])
        from__  = self.gc.get_vertex(from_["address"])
        self.compactify.add_move(from__, to_)
        #self.gc.del_vertex(from_["address"])
        #self.gc.add_vertex(from_["address"])
        self.moved_vertices.add(from__)

    def vertex_ctor(self, *, vertex, timestamp):
        vertex_ = self.gc.add_vertex(vertex["address"])
        self.log("vertex_ctor", timestamp, dict(
            vertex=self.compactify.add(vertex_),
        ))
    def vertex_dtor(self, *, vertex, timestamp):
        vertex_ = self.gc.del_vertex(vertex["address"])
        if vertex_ not in self.moved_vertices:
            self.log("vertex_dtor", timestamp, dict(
                vertex=self.compactify.add(vertex_),
            ))
    def vertex_move(self, *, from_, to, timestamp):
        to_ = self.gc.add_vertex(to["address"])
        from__ = self.gc.get_vertex(from_["address"])
        self.compactify.add_move(from__, to_)
        self.moved_vertices.add(from__)
        #self.gc.del_vertex(from_["address"])
        #self.gc.add_vertex(from_["address"])
        # if this sequence doesn't happen by itself, then we have a potentially serious bug
        # but I'm not sure if it can be easily detected by our script, as introducing
        # these lines means that sometimes, some stuff can't be found in started_waits

    def edge(self, *, pre, post, speculative=False, timestamp):
        pre_ = self.make_ptr(pre)
        post_ = self.make_ptr(post)
        self.log("edge", timestamp, dict(
            pre=self.compactify.add(pre_),
            post=self.compactify.add(post_),
            speculative=speculative,
        ))
    def sem_wait(self, *, sem, count, pre, post, timestamp):
        sem_, pre_, post_ = map(self.make_ptr_compactify, [sem, pre, post])

        self.started_waits[post_] = dict(
                timestamp=timestamp,
                sem=sem_,
                pre=pre_,
                post=post_,
                count=count
        )
    def sem_wait_completed(self, *, sem, post, timestamp):
        sem_, post_ = map(self.make_ptr_compactify, [sem, post])
        started = self.started_waits[post_]
        del self.started_waits[post_]

        pre_ = started["pre"]
        timestamp = started["timestamp"]
        count = started["count"]
        self.log("sem_wait", timestamp, dict(
            sem=sem_,
            pre=pre_,
            post=post_,
            count=count,
        ))
    sem_wait_cmpl = sem_wait_completed

    def sem_signal(self, *, sem, count, vertex, timestamp):
        sem_, vertex_ = map(self.make_ptr_compactify, [sem, vertex])
        self.log("sem_signal", timestamp, dict(
            sem=sem_,
            vertex=vertex_,
            count=count,
        ))
