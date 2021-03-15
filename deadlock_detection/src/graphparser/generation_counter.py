
class Generation:
    def __init__(self):
        self._version = -1
        self._created = False
        self.create()
    def create(self):
        if self._created:
            self.destroy()
            raise ValueError("vertex already exists")
        self._created = True
        self._version += 1
        return self._version
    def destroy(self):
        if not self._created:
            raise ValueError("vertex doesn't exist")
        self._created = False
        return self._version
    def version(self):
        return self._version
    def __repr__(self):
        return str(self.version())

class Vertex:
    def __init__(self, ptr, generation):
        self.ptr = ptr
        self.generation = generation
    def __eq__(self, other):
        try:
            return (self.ptr, self.generation) == (other.ptr, other.generation)
        except AttributeError:
            return False
    def __hash__(self):
        return self.ptr | (self.generation << 64)
    def __repr__(self):
        return f"<0x{self.ptr:x},{self.generation}>"
    def __str__(self):
        # This is self.__hash__ and not hash(self) on purpose.
        return str(self.__hash__())

class GenerationCounter:
    def __init__(self):
        self.generations = dict()
        self.add_vertex(0)
    def add_vertex(self, address):
        if not isinstance(address, int):
            raise TypeError("not an integer")
        if address not in self.generations:
            g = self.generations[address] = Generation()
            return Vertex(address, g.version())
        else:
            try:
                return Vertex(address, self.generations[address].create())
            except Exception as e:
                print("Vertex exists:", hex(address), address, self.generations[address])
                raise
    def del_vertex(self, address):
        if not isinstance(address, int):
            raise TypeError("not an integer")
        if address not in self.generations:
            raise ValueError(address)
        else:
            return Vertex(address, self.generations[address].destroy())
    def get_generation(self, address):
        if not isinstance(address, int):
            raise TypeError("not an integer")
        return self.generations[address]
    def get_vertex(self, address):
        if not isinstance(address, int):
            raise TypeError("not an integer")
        return Vertex(address, self.get_generation(address).version())

