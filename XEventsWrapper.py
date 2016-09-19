#!/usr/bin/env python
import os
import argparse
import time
import imp


def get_args():

    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--file', type=str, help='File to load', required=True)

    args = parser.parse_args()
    file_path = args.file

    return file_path


def load_from_file(filepath):

	#filepath = os.path.normpath(os.path.join(os.path.dirname(__file__), filepath))
	# Si llegamos hasta aca significa que existe filepath, y es un archivo de python
	filepath = os.path.abspath( filepath)
	path, file_name = os.path.split(filepath)
	mod_name, file_ext = os.path.splitext(file_name)

	if os.path.exists(filepath):
		try:
		    return imp.load_source(mod_name, filepath)
		except:
			pass


if __name__ == '__main__':

    #Get arguments
    file_path = get_args()

    #Check if the file path exists and it's a python file
    if (not (os.path.isfile(file_path) and file_path.lower().endswith(('.py')))):
        quit()
        
    #Dynamic module import
    module = load_from_file(file_path)

    tw = module.tw
    gobject = module.gobject
    gtk = module.gtk

    try:
        while True:
            tw.lc.start_time = module.time()
            tw.lc.icall(module.start)
            gobject.idle_add(tw.lc.doevalstep)
            gtk.main()
    except:
        pass

		