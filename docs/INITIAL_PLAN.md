## Initial Development Plan

Alright... lets work backwards and see what we definitely need for this

1. Robust CLI
1. Type Inference Engine
1. Smart Delimiter Detection (w/ Vectorization)
1. TUI Formatting
1. Schema inferencing

## Design Ideas

The first and most obvious requirement is the need for a `main.cpp` entrypoint to the application. This is where users will run `glance` and pass in arguments like `CSV_PATH` or `--head`, etc. Once this happens and everything is validated, move onto reading the csv into memory with `csv_reader.cpp`. This should also be pretty simple, running `mmap` on a page and setting all that mumbo jumbo up. Maybe we need to pin the page? Idk maybe there is some more nuance here. Once the csv is read into memory, we get the address it starts at and size and other metadata from `csv_reader` we start our main engine. I think maybe an interesting way to do this and something that would easily allow from a `--stream` flag is to have a thread on `tui.cpp` and constantly feed what we read into that program and collect results completely and then flush the results out on program finish. So, we'll gradually fill the `tui` up as we run our other pieces of code like `type_inference.cpp`, `schema_inference.cpp`, `validator.cpp`, `delim.cpp`. This sounds like a good plan for right now, definitely will be some cool things to add, but lets get started with this.
