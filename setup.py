from setuptools import setup, Extension


setup(
    name = "tokyotyrant",
    version = "0.1",
    ext_modules = [
        Extension(
            "tokyotyrant", ['tokyotyrant.c'],
            libraries=["tokyotyrant"]
        )
    ],
    description = """tokyotyrant aims to be a complete python wrapper for the 
        Tokyo Tyrant client library by Mikio Hirabayashi (http://1978th.net/).""",
    author = "Elisha Cook",
    author_email = "ecook@justastudio.com",
    url = "http://code.google.com/p/python-tokyotyrant-client/"
)
