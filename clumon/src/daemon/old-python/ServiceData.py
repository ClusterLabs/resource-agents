import string
import gettext
_ = gettext.gettext



class ServiceData:
  def __init__(self, name, state, owner, lastowner, restarts):
    self.name = name
    self.state = state
    self.owner = owner
    self.lastowner = lastowner
    self.restarts = restarts

  def getServiceProps(self):
    return (self.name, self.state, self.owner, self.lastowner, self.restarts)
