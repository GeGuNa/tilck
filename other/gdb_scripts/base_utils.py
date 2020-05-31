# SPDX-License-Identifier: BSD-2-Clause
# pylint: disable=unused-wildcard-import

import gdb # pylint: disable=import-error

type_task = gdb.lookup_type("struct task")
type_task_p = type_task.pointer()

type_process = gdb.lookup_type("struct process")
type_process_p = type_process.pointer()

list_node = gdb.lookup_type("struct list_node")
list_node_p = list_node.pointer()

def offset_of(type_name, field):
   return int(
      gdb.parse_and_eval(
         "((unsigned long)(&(({} *) 0)->{}))".format(type_name, field)
      )
   )

def container_of(elem_ptr, type_name, mem_name):
   off = offset_of(type_name, mem_name)
   expr = "(({} *)(((char *)0x{:08x}) - {}))".format(type_name, elem_ptr, off)
   return gdb.parse_and_eval(expr)
