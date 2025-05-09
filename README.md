# PLL 14xx parameter calculation tool

This tool is just a quick'n'dirty adaptation of functions that were included in newer linux kernel
versions (taken from 6.6), and which are able to calculate the pll parameters dynamically. 
On older kernel versions (5.15), for a new clock rate to be available, it needed to be added to the 
imx_pll1443x_tbl on the driver's source code.

Because the newer kernel now has the functions that correctly calculate the parameters, I decided to
create this small tool that uses them and shows all the parameters for a required clock rate. With this,
it becomes easier to add new entries to the table on kernel 5.15.

I don't plan to do any changes to this tool, use it if you feel that it helps.

## Build

It should be sufficient to call GCC directly to build it:

```
gcc pll.c -o pll
```

