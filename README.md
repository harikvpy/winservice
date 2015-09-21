# winservice
A dead simple C++ framework for writing Windows services

# Introduction

Windows services can be an intimidating experience for the novice. With its complexy callback mechanisms and load time limitations, programming one from scratch is usually done by copying the sample code from the SDK and then replacing a few things such as name, description, etc.

Sadly the sample code in the SDK is limited to C and C#. The only C++ class implementation for a Windows service (at the time of writing this code - circa 2012) comes in the form of a MFC class. However, using this class would result in the not-so-small MFC library being linked to your binary and being loaded during program startup.

This is where this framework comes in. As the title says, it's a very simple C++ class wrapper around the Windows services API that allows you to write a Windows service in minutes. No additional libraries to link to other than the standard C++ libraries.

The code also contains a simple framework that provides a C++ stream interface to a file logging API. The stream
based interface is typesafe and therefore can avoid nasty runtime errors that can occur when there is a mismatch
between the `printf()` format specifier and the actual arguments. That said, please note that stream based interface is much slower than printf() based standard C++ API and therefore this interface won't be a good candidate for using it in a time critical code.

# Notes
This code was originally published as part of an article for codeproject.com. You can find the original article that explains how to use the code at http://www.codeproject.com/Articles/781449/A-Simple-Cplusplus-Class-Framework-for-Services?msg=5081471#xx5081471xx.
