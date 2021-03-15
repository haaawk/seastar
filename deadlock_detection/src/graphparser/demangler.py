import subprocess

class Demangler:
    known_names = {}
    @staticmethod
    def strip_prefixes(name):
        prefixes = ["seastar::internal::", "seastar::", "std::chrono::"]
        for pref in prefixes:
            #name = name.removeprefix(pref)
            name = name.replace(pref, "")
        return name

    @staticmethod
    def demangle(*symbol_names):
        out = []
        for name in symbol_names:
            try:
                demangled = Demangler.known_names[name]
            except KeyError:
                demangled = subprocess.check_output(["c++filt", "-t", name]).decode().strip()
                Demangler.known_names[name] = demangled
            out.append(demangled)
        return [Demangler.strip_prefixes(x) for x in out]

