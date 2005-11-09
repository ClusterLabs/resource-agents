import string
import gettext
_ = gettext.gettext


MEMBER=_("Member")
JOINING=_("Joining")
DEAD=_("Dead")
UNKNOWN=_("Unknown")
NOT_APPLICABLE=_("N/A")


class NodeData:
  def __init__(self, votes, status, name):
    if votes == None:
      self.votes = NOT_APPLICABLE
    else:
      self.votes = votes
    self.votes = votes
    self.name = name
    stat = status.strip()
    if stat == "M":
      self.status = MEMBER
    elif stat == "J":
      self.status = JOINING
    elif stat == "X":
      self.status = DEAD
    else: 
      self.status = status

  def getNodeProps(self):
    return (self.name, self.votes, self.status)
