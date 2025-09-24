#!/usr/bin/env python3
"""
Main CLI entry point for Adobe Lightroom Cloud Access Tool
"""

import click
from lightroom_tool.auth import auth_commands
from lightroom_tool.photos import photo_commands
from lightroom_tool.cleanup_commands import cleanup


@click.group()
@click.version_option(version="0.1.0", prog_name="lightroom-tool")
@click.option('--verbose', '-v', is_flag=True, help='Enable verbose output')
@click.pass_context
def cli(ctx, verbose):
    """Adobe Lightroom Cloud Access Tool
    
    A command-line interface for accessing Adobe Lightroom Cloud with SSO authentication
    and rejected photos retrieval functionality.
    """
    ctx.ensure_object(dict)
    ctx.obj['verbose'] = verbose


# Add command groups
cli.add_command(auth_commands.auth)
cli.add_command(photo_commands.photos)
cli.add_command(cleanup)


if __name__ == '__main__':
    cli()
