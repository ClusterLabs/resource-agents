"""Exception class for throwing except on an 
   unsuccessfully run lvm command.
"""

class CommandError:
    def __init__(self, severity, message):
        self.__severity     = severity
        self.__msg          = message
    def getSeverity(self):
        return self.__severity
    def getMessage(self):
        return self.__msg

