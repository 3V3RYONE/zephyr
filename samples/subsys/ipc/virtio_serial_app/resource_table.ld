/*
 * Linker script snippet for resource table
 *
 * This ensures the resource table is placed at a known location
 * that Linux remoteproc can discover.
 *
 * Include this in your main linker script or let Zephyr handle it.
 */

SECTIONS
{
    /* Place resource table in a dedicated section */
    .resource_table : ALIGN(4)
    {
        KEEP(*(.resource_table))
    }
}
