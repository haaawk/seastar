from typing import Any
import json
import logging

from generation_counter import GenerationCounter, Vertex

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

class Compactify:
    def __init__(self):
        self.seen = {}
    def add(self, vertex):
        try:
            return self.seen[vertex]
        except KeyError:
            self.seen[vertex] = len(self.seen) + 1
            return len(self.seen)

class Parser:
    types = {
        "edge", "vertex_ctor", "vertex_dtor",
        "sem_signal", "sem_ctor",
        "semaphore_signal_schedule", "sem_dtor",
        "semaphore_wait"
    }

    def __init__(self):
        self.gc = GenerationCounter()
        self.compactify = Compactify()
        self._log = []

    def log(self, type_, timestamp, obj):
        obj["type"] = type_
        obj["timestamp"] = timestamp
        self._log.append(obj)

    def add_files(self, file_contents):
        objects = [
            json.loads(line) for file_ in file_contents for line in file_
        ]
        objects.sort(key=lambda event: event["timestamp"])
        for obj in objects:
            self.add_event(obj)
    def output(self, file_name: str):
        with open(file_name, "w") as f:
            for line in self._log:
                f.write(json.dumps(line) + "\n")
    def add_event(self, event: dict[str, Any]):
        logging.info(str(event))
        if event["type"] in {"attach_func_type"}:
            return
        try:
            fun = getattr(self, event["type"])
        except AttributeError:
            logging.warn(f"Ignoring event {event}")
            return

        del event["type"]
        try:
            fun(**event)
        except Exception as e:
            print(e, event)
            raise

    def make_ptr(self, ptr):
        try:
            addr = ptr["address"]
        except TypeError:
            addr = ptr
        return self.gc.get_vertex(addr)

    def sem_ctor(self, *, sem, timestamp=-1, **kwargs):
        sem_ = self.gc.add_vertex(sem["address"])
        self.log("sem_ctor", timestamp, dict(
            sem=self.compactify.add(sem_),
            count=sem["available_units"], 
        ))
    def sem_dtor(self, *, sem, timestamp, **kwargs):
        if kwargs:
            logging.warn(f"sem_dtor got sem={sem}, kwargs={kwargs}")
        sem_ = self.gc.del_vertex(sem["address"])
        self.log("sem_dtor", timestamp, dict(
            sem=self.compactify.add(sem_),
        ))
    def vertex_ctor(self, *, vertex, timestamp=-1, **kwargs):
        vertex_ = self.gc.add_vertex(vertex["address"])
        #self.log("vertex_ctor", timestamp, dict(
        #    vertex=self.compactify.add(vertex_),
        #))
    def vertex_dtor(self, *, vertex, timestamp=-1, **kwargs):
        vertex_ = self.gc.del_vertex(vertex["address"])
        #self.log("vertex_dtor", timestamp, dict(
        #    vertex=self.compactify.add(vertex_),
        #))
    def edge(self, *, pre, post, speculative=False, timestamp=-1, **kwargs):
        pre_ = self.make_ptr(pre)
        post_ = self.make_ptr(post)
        self.log("edge", timestamp, dict(
            pre=self.compactify.add(pre_),
            post=self.compactify.add(post_),
            #speculative=speculative,
        ))
    def sem_wait(self, *, sem, count, pre, post, timestamp):
        sem_ = self.make_ptr(sem)
        pre_ = self.make_ptr(pre)
        post_ = self.make_ptr(post)
        self.log("sem_wait", timestamp, dict(
            sem=self.compactify.add(sem_),
            pre=self.compactify.add(pre_),
            post=self.compactify.add(post_),
            count=count,
        ))
    def sem_wait_completed(self, *, sem, post, timestamp):
        sem_ = self.make_ptr(sem)
        post_ = self.make_ptr(post)
        #self.log("sem_wait_completed", timestamp, dict(
        #    sem=self.compactify.add(sem_),
        #    post=self.compactify.add(post_),
        #))
    def sem_signal(self, *, sem, count, vertex, timestamp):
        sem_ = self.make_ptr(sem)
        vertex_ = self.make_ptr(vertex)
        self.log("sem_signal", timestamp, dict(
            sem=self.compactify.add(sem_),
            vertex=self.compactify.add(vertex_),
            count=count,
        ))
    #def semaphore_signal_schedule(self, *, address, callee):
    #    semaphore = self.gc.get_vertex(address)
    #    self.log({"semaphore_signal_schedule": dict()})
