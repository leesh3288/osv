from tests.testing import *
import os
import subprocess
import json

@test
def aslr_test():
    NEEDLE = "ADDRESS_LIST:"

    runnner_script = os.path.join(osv_base, 'scripts', 'run.py')

    # get two address list
    output1 = subprocess.check_output([runnner_script, "-e", os.path.join("tests", "misc-aslr.so")]).decode()
    output2 = subprocess.check_output([runnner_script, "-e", os.path.join("tests", "misc-aslr.so")]).decode()

    # comapare each others
    output_json1 = json.loads(output1.split(NEEDLE)[1])
    output_json2 = json.loads(output2.split(NEEDLE)[1])
    
    # may fail in probability : it's ok, that's ASLR!
    for key in output_json1:
        addr1 = int(output_json1[key], 16)
        addr2 = int(output_json2[key], 16)
        assert(addr1 != addr2)
