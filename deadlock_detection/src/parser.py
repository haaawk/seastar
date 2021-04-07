import graph
import json

from typing import Dict
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

    def build_graph(self) -> tuple[graph.Node, Dict[int, graph.Semaphore]]:
        # Find all the semaphores
        semaphores = {}

        for elt in filter(lambda m: m["type"] == "sem_ctor", self.data):
            sem = elt["sem"]
            semaphores[sem] = graph.Semaphore(sem, elt["count"], sem)

        addr_to_node: Dict[int, graph.Node] = {-1: graph.Node(InvalidPost)}

        # Add the root of a tree and create all the nodes.
        # The nodes with operation None represent a single task.
        # The edges between nodes with None operation represent the order of creating the tasks.

        # Create all the nodes that have some incoming edges.
        for elt in filter(lambda m: m["type"] == "edge", self.data):
            if not elt["post"] in addr_to_node:
                addr_to_node[elt["post"]] = graph.Node(elt["post"])

        for elt in filter(lambda m: m["type"] == "edge", self.data):
            # If there is no node with address of the start of the edge,
            # then it has no incoming edges, so we add it to the children of the root.
            if not elt["pre"] in addr_to_node:
                node = graph.Node(elt["post"])
                addr_to_node[-1].add_child(node)
                addr_to_node[elt["pre"]] = node

            # The both start and end nodes of the edge
            # are created so we add an edge between them.
            addr_to_node[elt["pre"]].add_child(addr_to_node[elt["post"]])

        # We iterate through all of the operations and add a node for each of them.
        # Operations in self.df are sorted by the timestamp so when we add
        # a node, we preserve the order of operations.
        for elt in filter(lambda m: m["type"] == "sem_wait" or m["type"] == "sem_signal", self.data):
            if elt["type"] == "sem_wait":
                addr = elt["post"]
                new_node = graph.Node(addr, graph.Operation(elt["sem"], -elt["count"]))
            else:
                addr = elt["vertex"]
                new_node = graph.Node(addr, graph.Operation(elt["sem"], elt["count"]))

            # If the address 'addr' already maps to some Node,
            # then it means that there exist a task which run those operation.
            # We add the operation as the child of this task.
            # In the future, if we get an edge with start 'addr' and some end B then
            # it should mean that B happened after the operation.
            # Therefore the 'addr' should map to a node representing the operation,
            # so we give a new address to its parenting task and address 'addr' to operation.
            if addr in addr_to_node:
                self.update_free_addr(addr_to_node)
                addr_to_node[self.free_addr] = addr_to_node[addr]
            addr_to_node[addr] = new_node

            if elt["type"] == "sem_wait":
                addr_to_node[elt["pre"]].add_child(new_node)
            else:
                addr_to_node[self.free_addr].add_child(new_node)

        # As the nodes with None operation are meaningless in
        # the algorithm (they don't change the semaphore counters),
        # we delete them from the graph.
        addr_to_node[-1].erase_none()

        return addr_to_node[-1], semaphores
