import os
import gtk

class MountMatricula(object):
    def __init__(self):
        self.builder = gtk.Builder()
        self.builder.add_from_file("gui.ui")
        
        self.window = self.builder.get_object("dialog")

    def run(self):
        a = self.window.run()
        user = self.builder.get_object("username").get_text()
        pasw = self.builder.get_object("password").get_text()
        os.system("echo %(user)s %(password)s" % {"user": user, "password": pasw})

if __name__ == "__main__":
    m = MountMatricula()
    m.run()
