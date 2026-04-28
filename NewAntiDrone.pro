TEMPLATE = subdirs
SUBDIRS += core \
           test_comm

test_comm.depends = core
