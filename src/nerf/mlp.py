from typing import Optional, Tuple

import torch
from torch import nn


class MLP(nn.Module):
    r"""
    Multilayer Perceptron module.
    """

    def __init__(
        self,
        d_input: int = 3,
        n_layers: int = 8,
        d_filter: int = 256,
        skip: Tuple[int] = (4,),
        d_viewdirs: Optional[int] = None,
    ):
        super().__init__()
        self.d_input = d_input
        self.skip = skip
        self.act = nn.functional.relu
        self.d_viewdirs = d_viewdirs

        # Create model layers
        self.layers = nn.ModuleList(
            [nn.Linear(self.d_input, d_filter)]
            + [
                (
                    nn.Linear(d_filter + self.d_input, d_filter)
                    if i in skip
                    else nn.Linear(d_filter, d_filter)
                )
                for i in range(n_layers - 1)
            ]
        )

        # Bottleneck layers
        if self.d_viewdirs is not None:
            # If using viewdirs, split alpha and RGB
            self.alpha_out = nn.Linear(d_filter, 1)
            self.rgb_filters = nn.Linear(d_filter, d_filter)
            self.branch = nn.Linear(d_filter + self.d_viewdirs, d_filter // 2)
            self.output = nn.Linear(d_filter // 2, 3)
        else:
            # If no viewdirs, use simpler output
            self.output = nn.Linear(d_filter, 4)

    def forward(
        self, x: torch.Tensor, viewdirs: Optional[torch.Tensor] = None
    ) -> torch.Tensor:
        r"""
        Forward pass with optional view direction.
        """

        # Apply forward pass up to bottleneck
        x_input = x
        for i, layer in enumerate(self.layers):
            x = self.act(layer(x))
            if i in self.skip:
                x = torch.cat([x, x_input], dim=-1)

        # Apply bottleneck
        if viewdirs is not None:
            if self.d_viewdirs is None:
                # Cannot use viewdirs if instantiated with d_viewdirs = None
                raise ValueError(
                    "Cannot input x_direction if d_viewdirs was not given."
                )
            else:  # have both viewdirs and dimension of viewdirs
                # Split alpha from network output
                alpha = self.alpha_out(x)

                # Pass through bottleneck to get RGB
                x = self.rgb_filters(x)
                x = torch.concat([x, viewdirs], dim=-1)
                x = self.act(self.branch(x))
                x = self.output(x)

                # Concatenate alphas to output
                x = torch.concat([x, alpha], dim=-1)
        else:
            # Simple output
            x = self.output(x)

        return x
