# SPDX-License-Identifier: GPL-2.0+
#
# Entry-type module for external TPL binary
#

from binman.etype.blob_named_by_arg import Entry_blob_named_by_arg

class Entry_external_tpl(Entry_blob_named_by_arg):
    """External TPL binary

    Properties / Entry arguments:
        - external-tpl-path: Filename of file to read into the entry.

    This entry holds an external TPL binary.
    """
    def __init__(self, section, etype, node):
        super().__init__(section, etype, node, 'external-tpl')
        self.external = True
