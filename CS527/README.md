# cs527


# PUT and GET
## Filename Extension
By default, the files saved through PUT and GET will have `~` extension to their
original filename. For example, if client does a `get Makefile`, then the saved
file will be named as `Makefile~`.

## Append Mode
By default, the PUT and GET will use `O_APPEND` mode to open file. This ensures we
will not override existing files.

# Contacts
- Yizhou Shan <ys@purdue.edu>
- Liwei Guo <guo405@purdue.edu>
- Yejie Geng <geng18@purdue.edu>
