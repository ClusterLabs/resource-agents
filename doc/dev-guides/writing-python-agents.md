# Resource Agent guide for Python

## Introduction

A simple library for authoring resource agents in Python is
provided in the `ocf.py` library.

Agents written in Python should be compatible with Python 3.6+.

The library provides various helper constants and functions, a logging
implementation as well as a run loop and metadata generation facility.

## Constants

The following OCF constants are provided:

* `OCF_SUCCESS`
* `OCF_ERR_GENERIC`
* `OCF_ERR_ARGS`
* `OCF_ERR_UNIMPLEMENTED`
* `OCF_ERR_PERM`
* `OCF_ERR_INSTALLED`
* `OCF_ERR_CONFIGURED`
* `OCF_NOT_RUNNING`
* `OCF_RUNNING_MASTER`
* `OCF_FAILED_MASTER`
* `OCF_RESOURCE_INSTANCE`
* `HA_DEBUG`
* `HA_DATEFMT`
* `HA_LOGFACILITY`
* `HA_LOGFILE`
* `HA_DEBUGLOG`
* `OCF_ACTION` -- Set to `$__OCF_ACTION` if set, or to the first command line argument.

## Logger

The `logger` variable holds a Python standard log object with its
formatter set to follow the OCF standard logging format.

Example:

``` python

from ocf import logger

logger.error("Something went terribly wrong.")

```

## Helper functions

* `ocf_exit_reason`: Prints the exit error string to stderr.
* `have_binary`: Returns True if the given binary is available.
* `is_true`: Converts an OCF truth value to a Python boolean.
* `is_probe`: Returns True when running a probe action. Used to return
  OCF_NOT_RUNNING instead of error code that would cause unexpected actions
  like fencing before starting the resource or when it is disabled.
* `get_parameter`: Looks up the matching `OCF_RESKEY_` environment variable.
* `distro`: Returns <Distro>/<Version> or <Distro> if version info is unavailable.
* `Agent`: Class which helps to generate the XML metadata.
* `run`: OCF run loop implementation.

## Run loop and metadata example

``` python
import os
import sys

OCF_FUNCTIONS_DIR = os.environ.get("OCF_FUNCTIONS_DIR", "%s/lib/heartbeat" % os.environ.get("OCF_ROOT"))
sys.path.append(OCF_FUNCTIONS_DIR)
import ocf

def start_action(argument):
    print("The start action receives the argument as a parameter: {}".format(argument))


def main():
    agent = ocf.Agent("example-agent",
                      shortdesc="This is an example agent",
                      longdesc="An example of how to " +
                      "write an agent in Python using the ocf " +
                      "Python library.")
    agent.add_parameter("argument",
                        shortdesc="Example argument",
                        longdesc="This argument is just an example.",
                        content_type="string",
                        default="foobar")
    agent.add_action("start", timeout=60, handler=start_action)
    agent.run()

if __name__ == "__main__":
    main()
```
