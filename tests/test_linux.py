import numpy as np
from time import sleep
from test_common import *
import logging
import os
logging.basicConfig(filename="log.txt", level=logging.DEBUG, filemode='w', 
                    format='%(asctime)s,%(msecs)d %(name)s %(levelname)s %(message)s',
                    datefmt='%H:%M:%S',)

sonata_path = "../../sonata" #TODO: make this better
print("Copying sonata.bit...")
copy_sonata_bitstream(sonata_path)
os.sync() #ensure above write finishes

print ("Done copy. Testing config write and parse...")

write_all_options(sonata_path)
os.sync() #ensure above write finishes

print("Done. Ejecting drive...")
sleep(0.25)

results, full_file = get_test_results(sonata_path)
intended_failures = (3, 4) # these ones check to make sure invalid 
any_failures = 0
for num, result in enumerate(results):
    if num in intended_failures:
        if result['passed']:
            print("Passed test that should have failed {}".format(str(result)))
            any_failures = 1
    else:
        if not result['passed']:
            print("Failed test {}".format(str(result)))
            any_failures = 1

with open('ERROR.txt', "w") as f:
    f.write(str(full_file))

if not any_failures:
    print("All tests passed")
# print(full_file)