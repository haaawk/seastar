from typing import Dict
import json

import graph

InvalidPost = -5


class GraphParser:
    data: list[Dict]
    free_addr: int  # The smallest free address for graph nodes

    # Initializes GraphParser with data being a list of JSONs
    def __init__(self, data):
        data = list(map(lambda a: json.loads(a), data))
        data.sort(key=lambda a: a["timestamp"])
        self.data = data
        self.free_addr = 0

    def update_free_addr(self, addresses):
        while self.free_addr in addresses:
            self.free_addr += 1

    def get_node(self, addr_to_node: Dict[int, graph.Node], addr: int, *args) -> graph.Node:
        # If the address 'addr' already maps to some Node,
        # then it means that there exist a task which run those operation.
        # We add the operation as the child of this task.
        # In the future, if we get an edge with start 'addr' and some end B then
        # it should mean that B happened after the operation.
        # Therefore the 'addr' should map to a node representing the operation,
        # so we give a new address to its parenting task and address 'addr' to operation.
        new_node = graph.Node(addr, *args)
        if addr in addr_to_node:
            self.update_free_addr(addr_to_node)
            addr_to_node[addr].add_child(new_node)
        else:
            addr_to_node[-1].add_child(new_node)
        addr_to_node[addr] = new_node
        return new_node

    def build_graph(self) -> tuple[graph.Node, Dict[int, graph.Semaphore]]:
        # Find all the semaphores
        semaphores = {}

        for elt in filter(lambda m: m["type"] == "sem_ctor", self.data):
            sem = elt["sem"]
            sem_operations = list(filter(lambda m: m.get("sem", -69) == sem, self.data))
            assert (len(sem_operations) > 0)
            semaphores[sem] = graph.Semaphore(sem, elt["count"], sem, sem_operations[0]["timestamp"],
                                              sem_operations[-1]["timestamp"])

        addr_to_node: Dict[int, graph.Node] = {-1: graph.Node(InvalidPost)}

        # Add the root of a tree and create all the nodes.
        # The nodes with operation None represent a single task. 
        # The edges between nodes with None operation represent the order of creating the tasks.

        # We iterate through all of the operations and add a node for each of them.
        # Operations in self.df are sorted by the timestamp so when we add 
        # a node, we preserve the order of operations.
        for elt in filter(lambda m: m["type"] == "sem_wait" or m["type"] == "sem_signal" or m["type"] == "edge",
                          self.data):
            if elt["type"] == "edge":
                pre_node = self.get_node(addr_to_node, elt["pre"])
                post_node = self.get_node(addr_to_node, elt["post"])
                pre_node.add_child(post_node)
            elif elt["type"] == "sem_wait":
                pre_node = self.get_node(addr_to_node, elt["pre"])
                post_node = self.get_node(addr_to_node, elt["post"], graph.Operation(elt["sem"], -elt["count"]))
                pre_node.add_child(post_node)
            else:
                _ = self.get_node(addr_to_node, elt["vertex"], graph.Operation(elt["sem"], elt["count"]))

        # As the nodes with None operation are meaningless in
        # the algorithm (they don't change the semaphore counters),
        # we delete them from the graph.
        addr_to_node[-1].erase_none()

        return addr_to_node[-1], semaphores
