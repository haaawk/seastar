from typing import Union, List, Dict, Set
import copy
import random


# Class signifying one of semaphore operations wait and signal
#   semaphore_id : id of the semaphore on which the operation is performed
#   modification : when positive the operation is signal and the operation is incrementing the semaphore by modification
#                  when negative the operation is wait and the operation is decrementing the semaphore by -modification
#   process : the number of process to which the operation belongs. It is a fictional process, needed in order to know
#             which operations can be performed at which time.
class Operation:
    semaphore_id: Union[int, None]
    modification: int

    def __init__(self, semaphore_id: Union[int, None], modification: int):
        self.semaphore_id = semaphore_id
        self.modification = modification

    def __str__(self):
        if self.modification == 0:
            return "Empty operation\n"

        if self.modification > 0:
            return "Signal with " + str(self.modification) + " on semaphore with id " + str(self.semaphore_id) + "\n"

        return "Wait for " + str(-self.modification) + " on semaphore with id " + str(self.semaphore_id) + "\n"

    # If modification is 0 then the operation does nothing
    def is_none(self):
        return self.modification == 0


# Class signifying a semaphore operations wait and signal
#   id : semaphore's id
#   count : semaphore count
#   queue : FIFO queue pairs of:
#           1) operation waiting on the semaphore
#           2) index of the operation in the execution chain
#   restricted_processes : if operation o = queue[i][0] then no operation
#                          with process p may be executed if p is in restricted_processes[i]
#                          while o remains in the queue
class Semaphore:
    id: int
    original_id: int
    count: int
    queue: list[tuple['Node', int]]

    def __init__(self, id: int, count: int, original_id: int):
        self.id = id
        self.count = count
        self.queue = []
        self.original_id = original_id

    def __str__(self):
        basic_info = "Semaphore with id " + str(self.id) + " has " + str(self.count) + " units\n"
        if len(self.queue) == 0:
            return basic_info + "\n"

        queue_info = "Semaphore's queue:\n"
        for node in self.queue:
            queue_info += "Index in execution chain: " + str(node[1]) + "\n"
            queue_info += str(node[0])

        return basic_info + queue_info + "\n"

    def get_debug_information(self) -> dict[str, any]:
        result = {"sem_id": self.original_id, "unit_count": self.count, "waiting": len(self.queue)}
        result["queue"] = [operation.get_debug_info() for operation, _ in self.queue]
        return result

    # Performs the given operation, that has a given index and restricted processes set
    def do_operation(self, operation: 'Node', index: int) -> bool:
        if self.count + operation.operation.modification < 0:
            self.queue.append((operation, index))
            return False

        self.count += operation.operation.modification
        return True

    # If the first operation waiting on the semaphore can be executed,
    # it is deleted from the waiting queue and returned together with its index
    # in the execution chain
    # If there is no such operation None, None is returned
    def try_get_operation(self) -> Union[tuple['Node', int], tuple[None, int]]:
        if len(self.queue) != 0:
            if self.count + self.queue[0][0].operation.modification >= 0:
                operation, index = self.queue.pop(0)
                return operation, index

        return None, -1


# Class signifying a program execution
#   semaphores : map from semaphore id to semaphore, containing all the semaphores
#                created during program's execution
#   execution_chain : the list of the operations to be executed
#   executed : the list such that executed[i] = True iff execution_chain[i] was executed
#   process_dependency : if k is in process_dependency[j] then j can't run before k unless j == k
#   first_possible_index : first_possible_index = n iff for each 0 <= i < n executed[i] = True
#   executed_count : number of executed operations from the execution_chain
class Execution:
    semaphores: Dict[int, Semaphore]
    execution_chain: List['Node']
    executed: List[int]
    first_possible_index: int
    executed_count: int
    previous_node_finished_count: Dict['Node', int]
    waiting_on_semaphore: Set['Node']

    def __init__(self, semaphores: Dict[int, Semaphore], execution_chain: List['Node']):
        self.semaphores = copy.deepcopy(semaphores)
        self.execution_chain = execution_chain
        self.executed = [0] * (len(execution_chain))
        self.first_possible_index = 0
        self.executed_count = 0
        self.previous_node_finished_count = dict()
        self.waiting_on_semaphore = set()

    def __str__(self):
        result = ""
        result += "Semaphores' state:\n"
        for _, semaphore in self.semaphores.items():
            result += str(semaphore)

        result += "\n"

        if len(self.execution_chain) <= 20:
            for index, node in enumerate(self.execution_chain):
                if self.executed[index] == 1:
                    result += "\u001b[32m" + "Executed: \u001b[0m\n"
                else:
                    result += "\u001b[31m" + "Not executed: \u001b[0m\n"

                result += str(node)
                result += "\n"

        else:
            for index in range(self.first_possible_index,
                               min(len(self.execution_chain), self.first_possible_index + 20)):
                if self.executed[index] == 1:
                    result += "Operation with index " + str(index) + " was executed: \n"
                else:
                    result += "Operation with index " + str(index) + " was not executed: \n"

                result += str(self.execution_chain[index])

        return result

    def get_debug_info(self) -> [dict[str, any]]:
        result = []
        for semaphore in self.semaphores.values():
            result.append(semaphore.get_debug_information())

        return result

    # Function that checks if the given operation can be executed
    def is_executable(self, operation: 'Node') -> bool:
        return self.previous_node_finished_count.get(operation,
                                                     0) == operation.prev_count and operation not in self.waiting_on_semaphore

    def mark_executed(self, operation: 'Node') -> None:
        for child in operation.next:
            self.previous_node_finished_count[child] = self.previous_node_finished_count.get(child, 0) + 1

    # Function that tries to execute the given operation with the given index
    # if operation needs to wait on its semaphore then returns False
    # Otherwise returns True
    def do_operation(self, operation: 'Node', index: int) -> bool:
        assert(self.executed[index] == 0)
        if operation.operation.is_none():
            self.mark_executed(operation)
            self.executed_count += 1
            self.executed[index] = 1

            if index == self.first_possible_index:
                self.first_possible_index += 1
                while self.first_possible_index < len(self.executed) and self.executed[self.first_possible_index]:
                    self.first_possible_index += 1

            return True

        operation_semaphore: Semaphore = self.semaphores[operation.operation.semaphore_id]

        if not operation_semaphore.do_operation(operation, index):
            self.waiting_on_semaphore.add(operation)
            return False

        else:
            self.mark_executed(operation)
            self.executed_count += 1
            self.executed[index] = 1

            if index == self.first_possible_index:
                self.first_possible_index += 1
                while self.first_possible_index < len(self.executed) and self.executed[self.first_possible_index]:
                    self.first_possible_index += 1
            return True

    # A function that looks for a next operation that can be executed
    # returns that operation together with its index if one is found,
    # returns None, -1 otherwise
    def next_possible_operation(self) -> tuple[Union['Node', None], int]:
        # If all the operations where executed there is no next operation
        if self.first_possible_index >= len(self.execution_chain):
            return None, -1

        # If the first possible operation can be executed it is returned
        if self.is_executable(self.execution_chain[self.first_possible_index]):
            assert(self.executed[self.first_possible_index] == 0)
            return_index = self.first_possible_index

            return self.execution_chain[return_index], return_index

        # The semaphores are checked to find a next operation
        for _, semaphore in self.semaphores.items():
            operation, index = semaphore.try_get_operation()
            if operation is not None:
                self.waiting_on_semaphore.remove(operation)

                return operation, index

        # Execution chain is checked for executable operations
        for i in range(self.first_possible_index + 1, len(self.execution_chain)):
            if not self.executed[i] and self.is_executable(self.execution_chain[i]) and self.execution_chain[
                i].operation.modification >= 0:

                return self.execution_chain[i], i

        return None, -1

    # A function that attempts to execute all the operation from the execution chain
    # returns False if it fails and True otherwise
    def try_executing(self) -> bool:
        assert(len(self.previous_node_finished_count) == 0)
        while True:
            next_operation, index = self.next_possible_operation()
            if next_operation is None:
                if self.executed_count >= len(self.execution_chain):
                    return True
                return False

            self.do_operation(next_operation, index)


# A class signifying the execution graph's Nodes. There exist an
# edge from Node A to Node B iff the operation in Node A must be
# executed before the operation in Node B
#   operation : the operation to be executed
#   next : list of edges from this Node to other
class Node:
    operation: Operation
    next: Set['Node']
    prev_count: int
    original_post: int

    def __init__(self, original_post: int, operation: Operation = Operation(None, 0)):
        self.original_post = original_post
        self.operation = operation
        self.next = set()
        self.prev_count = 0

    def __str__(self):
        return str(self.operation)

    def get_debug_info(self) -> dict[str, Union[str, int]]:
        operation = self.operation
        post = self.original_post
        type = ""
        count = operation.modification
        if count > 0:
            type = "signal"
        if count == 0:
            type = "none"
        if count < 0:
            type = "wait"
            count = -count

        return {"original_post": post, "type": type, "count": count}

    def add_parent(self):
        self.prev_count += 1

    def add_child(self, child: 'Node'):
        self.next.add(child)
        child.add_parent()

    def is_none(self):
        return self.operation.is_none()

    # erases Nodes with None operation everywhere except for the first one
    def erase_none(self):
        ind = 0
        next_list = list(self.next)

        while ind < len(next_list):
            if (next_list[ind]).is_none():
                next_list += list(next_list[ind].next)
                next_list.pop(ind)

            else:
                ind += 1

        self.next = set(next_list)

        for child in self.next:
            child.erase_none()


# Class Graph stores the information about semaphore operation tree and process dependencies.
# It implements method detecting occured and possible deadlocks - is_deadlock_free.
class Graph:
    root: Node
    semaphores: Dict[int, Semaphore]

    def __init__(self, graph: Node, semaphores: Dict[int, Semaphore]):
        self.root = copy.deepcopy(graph)
        self.semaphores = semaphores

    # Simplifies graph by deleting all the operations that are done on the semaphores 
    # which are not in the semaphore_addresses list.
    def simplify_graph(self, node, semaphore_addresses):
        if node.operation.semaphore_id not in semaphore_addresses:
            node.operation.modification = 0

        for next in node.next:
            self.simplify_graph(next, semaphore_addresses)

    # Gets some execution chain and checks if it can lead to a deadlock.
    # Returns true if there is no deadlock.
    def check_execution(self, execution_chain: List['Node']) -> tuple[bool, Union[Execution, None]]:
        execution = Execution(self.semaphores, execution_chain)
        res = execution.try_executing()
        if res:
            return res, None
        return res, execution

    # Generate all possible execution chains. For each of them run check_execution.
    # All the possible execution chains are in fact all the topological sorts of our graph.
    # The method uses algorithm that finds all topological sorts of DAG
    # (https://www.geeksforgeeks.org/all-topological-sorts-of-a-directed-acyclic-graph/).
    # Returns true if there is no deadlock.
    def check_all_possible_executions(self, res: List['Node'], nodes: List[Node]) -> tuple[
        bool, Union[Execution, None]]:
        flag = False

        for node in list(nodes):
            for next in node.next:
                nodes.append(next)

            nodes.remove(node)

            res.append(node)

            result, execution = self.check_all_possible_executions(res, nodes)
            if not result:
                return result, execution

            res.pop(-1)

            for next in node.next:
                nodes.remove(next)

            nodes.append(node)

            flag = True

        if not flag:
            return self.check_execution(res)

        return True, None

    # For each subset of semaphores of size not greater than 3, 
    # remove from graph all the semaphores except those in the subset.
    # Then run check_all_possible_executions on the obtained graph.
    # Returns true if there is no deadlock.
    def is_deadlock_free(self) -> tuple[bool, Union[None, Execution]]:
        for _, i in self.semaphores.items():
            for _, j in self.semaphores.items():
                for _, k in self.semaphores.items():
                    if k.id >= j.id >= i.id:
                        simplified_graph = copy.deepcopy(self)
                        simplified_graph.simplify_graph(simplified_graph.root,
                                                        [sem.id for sem in [i, j, k]])
                        simplified_graph.semaphores = {sem.id: copy.deepcopy(sem) for sem in [i, j, k]}
                        simplified_graph.root.erase_none()
                        res, execution = simplified_graph.check_all_possible_executions([], [simplified_graph.root])
                        if not res:
                            return res, execution
        return True, None
