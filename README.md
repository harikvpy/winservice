# winservice
A dead simple C++ framework for writing Windows services

# Introduction

Windows services can be an intimidating experience for the novice. With its complexy callback mechanisms and load time limitations, programming one from scratch is usually done by copying the sample code from the SDK and then replacing a few things such as name, description, etc.

Sadly the sample code in the SDK is limited to C and C#. The only C++ class implementation for a Windows service (at the time of writing this code - circa 2012) comes in the form of a MFC class. Unfortunately, using this class would result in the not-so-small MFC library being linked to your service and being loaded during program startup.

This is where this framework comes in. As the title says, it's a very simple C++ class wrapper around the Windows services API that allows you to write a Windows service in minutes. No additional libraries to link to other than the standard C++ libraries.

# Notes
This code was originally published as part of an article for codeproject.com. I'm archiving the code here as part of an effort to centralize all my open source contributions. You can find the original article that explains how to use the code at http://www.codeproject.com/Articles/781449/A-Simple-Cplusplus-Class-Framework-for-Services?msg=5081471#xx5081471xx.
