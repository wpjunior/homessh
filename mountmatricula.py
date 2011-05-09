import os
import gtk
import pexpect
import gobject

SERVIDOR = "radioserra.com.br"
MOUNT_POINT = "/tmp/teste/"
DEBUG = True

def debug_info(msg):
    if DEBUG:
        print "# %s" % msg

class Mounter(object):
    def __init__(self, username, password):
        self.username = username
        self.password = password
        self.initial_responses = ['Are you sure you want to continue connecting (yes/no)?',
                                  'password:', pexpect.EOF]
        
    def mount(self, timeout=120):
        parms = {"username": self.username, "server": SERVIDOR, "mount_point": MOUNT_POINT}
        cmd = "sshfs %(username)s@%(server)s:/home/%(username)s/ %(mount_point)s" % parms

        child = pexpect.spawn(cmd)

        ## Get the first response.
        ret = child.expect (self.initial_responses, timeout)
        ## The first reponse is to accept the key.
        if ret==0:
            debug_info("The first reponse is to accept the key.")
            #~ T = child.read(100)
            child.sendline("yes")
            child.expect('password:', Timeout)
            child.sendline(self.password)
        ## The second response sends the password.
        elif ret == 1:
            debug_info("The second response sends the password.")
            child.sendline(self.password)
        ## Otherwise, there is an error.
        else:
            debug_info("Otherwise, there is an error.")
            return (-3, 'ERROR: Unknown: ' + str(child.before))

        ## Get the next response.
        Possible_Responses = ['password:', pexpect.EOF]
        ret = child.expect (Possible_Responses, timeout)

        
        ## If it asks for a password, error.
        if ret == 0:
            debug_info("If it asks for a password, error.")
        elif ret == 1:
            debug_info("Otherwise we are okay.")
            ## Otherwise we are okay.
        else:
            debug_info("Otherwise, there is an error.")
            debug_info('ERROR: Unknown: ' + str(child.before))

        gtk.main_quit()
        

class MountMatricula(object):
    def __init__(self):
        self.builder = gtk.Builder()
        self.builder.add_from_file("gui.ui")
        self.builder.connect_signals(self)
        self.window = self.builder.get_object("dialog")

    def values_changed_cb(self, obj):
        u = len(self.builder.get_object("username").get_text())
        p = len(self.builder.get_object("password").get_text())

        self.builder.get_object("btn_connect").set_sensitive((u>1) and (p>1))

    def run(self):
        response = self.window.run()
        user = self.builder.get_object("username").get_text()
        pasw = self.builder.get_object("password").get_text()
        self.window.destroy()

        if not response:
            return
        
        self.mounter = Mounter(user, pasw)
        gobject.idle_add(self.mounter.mount)

if __name__ == "__main__":
    os.system('fusermount -u %s' % MOUNT_POINT)
    m = MountMatricula()
    m.run()

    gtk.main()
