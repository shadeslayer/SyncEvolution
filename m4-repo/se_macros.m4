#serial 20110803

## _SE_VERSIONS(MAJOR_VERSION, MINOR_VERSION, MICRO_VERSION, NANO_VERSION)
##
## exports MICRO_VERSION and NANO_VERSION to se_test_micro_version and
## se_test_nano_version defines respectively. If any of passed parameters are
## empty, those defines are defined as '0'. MAJOR_VERSION and MINOR_VERSION are
## ignored.
##
m4_define([_SE_VERSIONS],
          [dnl
           m4_ifval([$3],
                    [dnl nonempty part
                     m4_define([se_test_micro_version], [$3])
                    ],
                    [dnl empty part
                     m4_define([se_test_micro_version], [0])
                    ]
                   )[]dnl
           m4_ifval([$4],
                    [dnl nonempty part
                     m4_define([se_test_nano_version], [$4])
                    ],
                    [dnl empty part
                     m4_define([se_test_nano_version], [0])
                    ]
                   )[]dnl
          ]
         )[]dnl

## SE_CHECK_FOR_STABLE_RELEASE
##
## No-op if STABLE_RELEASE is already defined.
## Defines STABLE_RELEASE to 'yes' if nano and micro versions are greater or
## equal 99.5. (So 1.1.99.5b is counted as table release.)
## If above condition is not true then STABLE_RELEASE is defined to 'yes' if
## current version is not dirty (nothing is appended by gen-git-version.sh).
## If above fails, then STABLE_RELEASE is defined to 'no'.
##
AC_DEFUN([SE_CHECK_FOR_STABLE_RELEASE],
         [m4_ifndef([STABLE_RELEASE],
                    [dnl ifndef part
                     m4_define([se_test_plus_index], m4_index(AC_PACKAGE_VERSION, [+]))[]dnl
                     m4_if(se_test_plus_index, [-1],
                           [dnl if part
                            m4_define([se_test_version], AC_PACKAGE_VERSION)
                           ],
                           [dnl else part
                            m4_define([se_test_version], m4_substr(AC_PACKAGE_VERSION, [0], se_test_plus_index))
                            m4_define([se_test_dirty_version], [yes])
                           ]
                          )[]dnl
                     _SE_VERSIONS(m4_bpatsubst(se_test_version, [[^0-9A-Za-z]+], [,]))[]dnl

                     m4_if(se_test_micro_version, [99],
                           [dnl if part
                            m4_define([se_test_nano_number], m4_translit(se_test_nano_version, [A-Za-z]))
                            m4_if(m4_eval(se_test_nano_number [>= 5]), [1],
                                  [dnl if part
                                   m4_define([STABLE_RELEASE], [yes])
                                  ]
                                 )
                           ],
                           [dnl else part
                            m4_ifndef([se_test_dirty_version],
                                      [dnl ifndef part
                                       m4_define([STABLE_RELEASE], [yes])
                                      ]
                                     )
                           ]
                          )[]dnl
                     m4_ifndef([STABLE_RELEASE],
                               [dnl ifndef part
                                m4_define([STABLE_RELEASE], [no])
                               ]
                              )
                     dnl macros cleanup
                     m4_ifdef([se_test_plus_index],
                              [dnl ifdef part
                               m4_undefine([se_test_plus_index])
                              ]
                             )[]dnl
                     m4_ifdef([se_test_version],
                              [dnl ifdef part
                               m4_undefine([se_test_version])
                              ]
                             )[]dnl
                     m4_ifdef([se_test_dirty_version],
                              [dnl ifdef part
                               m4_undefine([se_test_dirty_version])
                              ]
                             )[]dnl
                     m4_ifdef([se_test_micro_version],
                              [dnl ifdef part
                               m4_undefine([se_test_micro_version])
                              ]
                             )[]dnl
                     m4_ifdef([se_test_nano_version],
                              [dnl ifdef part
                               m4_undefine([se_test_nano_version])
                              ]
                             )[]dnl
                     m4_ifdef([se_test_nano_number],
                              [dnl ifdef part
                               m4_undefine([se_test_nano_number])
                              ]
                             )[]dnl
                    ]
                   )[]dnl
         ]
        )

## SE_ENABLE_BACKENDS_PRE
##
## Marks BACKEND_DEFINES and SYNCSOURCES as variables to be substituted.
## For internal use only.
##
AC_DEFUN([SE_ENABLE_BACKENDS_PRE],
         [AC_SUBST(SYNCSOURCES)
          AC_SUBST(BACKEND_DEFINES)
          BACKENDS=''
          BACKEND_DEFINES=''
          SYNCSOURCES=''
         ])

## SE_ARG_ENABLE_BACKEND(BACKEND, DIR, HELP-STRING, [ACTION-IF-GIVEN],
##                       [ACTION-IF-NOT-GIVEN])
##
## Same as AC_ARG_ENABLE(), but also tells configure that the
## backend exists.
##
## BACKEND = name of modules built in that dir as .la files without the
##           obligatory sync prefix, e.g. "ebook"
## DIR = name of the directory inside src/backends, e.g., "evolution"
##
AC_DEFUN([SE_ARG_ENABLE_BACKEND],
         [AC_REQUIRE([SE_ENABLE_BACKENDS_PRE])
          AC_ARG_ENABLE($1, $3, $4, $5)
          BACKENDS="$BACKENDS $1"
          BACKEND_DEFINES="$BACKEND_DEFINES ENABLE_[]m4_translit($1, [a-z], [A-Z])"
          for source in $2
          do
            SYNCSOURCES="$SYNCSOURCES src/backends/$2/sync$1.la"
          done
         ]
        )

## SE_ADD_BACKENDS
##
## Adds backends available under src/backends. See build/gen-backends.sh script.
##
AC_DEFUN([SE_ADD_BACKENDS],
         [m4_esyscmd(build/gen-backends.sh)
         ]
        )

## SE_GENERATE_AM_FILES
##
## Generates some .am files needed by automake.
##
AC_DEFUN([SE_GENERATE_AM_FILES],
         [m4_syscmd(build/gen-backends-am.sh)
         ]
        )

## SE_GENERATE_LINGUAS
##
## Generates LINGUAS file.
##
AC_DEFUN([SE_GENERATE_LINGUAS],
         [m4_syscmd(build/gen-linguas.sh)
         ]
        )
