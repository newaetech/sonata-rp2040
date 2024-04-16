import numpy as np
from time import sleep
from test_common import *
import logging

# shutil.copyfile("")
# logging.getLogger().setLevel(logging.DEBUG)
# ps_run("./eject.ps1")
logging.basicConfig(filename="log.txt", level=logging.DEBUG, filemode='w', 
                    format='%(asctime)s,%(msecs)d %(name)s %(levelname)s %(message)s',
                    datefmt='%H:%M:%S',)
sonata_letter = get_windows_drive_letter()

if not sonata_letter:
    print("Sonata drive not found")
    exit(1)

print("Sonata at " + sonata_letter)
print("Copying sonata.bit...")
copy_sonata_bitstream(sonata_letter)

print ("Done copy. Testing config write and parse...")

write_all_options(sonata_letter)

print("Done. Ejecting drive...")
sleep(0.25)
win_eject_drive()

results, full_file = get_test_results(sonata_letter)
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