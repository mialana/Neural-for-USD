import torch
import numpy as np
import matplotlib.pyplot as plt
import math
from matplotlib.gridspec import GridSpec
from mpl_toolkits.mplot3d import Axes3D

from typing import Callable
import PIL.Image as PILImage

import cv2
from torchvision.transforms import ToPILImage
import os
import re

import questionary
import click

import argparse

from train_nerf import NeRFState, init_models, device, near, far, chunksize
from train_nerf import (
    kwargs_sample_hierarchical,
    kwargs_sample_stratified,
    n_samples_hierarchical,
)
from nerf import get_rays, nerf_forward

to_PIL: Callable[[torch.Tensor], PILImage.Image] = ToPILImage()

DEFAULT_RADIUS = 3.5


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--asset-name", type=str, default="simpleCube")
    return parser.parse_args()


def create_video(image_folder, video_path, fps=16):
    def sort_key(filename):
        match = re.search(r"(\d+)", filename)
        return int(match.group()) if match else 0

    filenames = sorted(
        [
            os.path.join(image_folder, f)
            for f in os.listdir(image_folder)
            if f.endswith(".png") or f.endswith(".jpg")
        ],
        key=sort_key,
    )

    if not filenames:
        print("No images found for video.")
        return

    # Get frame size from first image
    first_frame = cv2.imread(filenames[0])
    height, width, _ = first_frame.shape

    # Define video writer (H.264/MP4)
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(video_path, fourcc, fps, (width, height))

    for fname in filenames:
        frame = cv2.imread(fname)
        writer.write(frame)

    writer.release()
    print(f"Saved video to {video_path}")


def replicate_view(state, test_idx: int):
    test_img = state.images[test_idx]
    test_pose = state.poses[test_idx]

    # Generate rays
    height, width = test_img.shape[:2]
    rays_o, rays_d = get_rays(height, width, state.focal, test_pose)
    rays_o = rays_o.reshape([-1, 3])
    rays_d = rays_d.reshape([-1, 3])

    # Run NeRF forward pass
    outputs = nerf_forward(
        rays_o,
        rays_d,
        near,
        far,
        state.encode,
        state.model,
        curr_kwargs_sample_stratified=kwargs_sample_stratified,
        curr_n_samples_hierarchical=n_samples_hierarchical,
        curr_kwargs_sample_hierarchical=kwargs_sample_hierarchical,
        curr_fine_model=state.fine_model,
        viewdirs_encoding_fn=state.encode_viewdirs,
        curr_chunksize=chunksize,
    )

    # Get the RGB image
    rgb = outputs["rgb_map"]
    rendered_img = rgb.reshape(height, width, 3).detach().cpu().numpy()

    # Plot side-by-side
    fig, axes = plt.subplots(1, 2, figsize=(10, 5))
    axes[0].imshow(test_img.cpu().numpy())
    axes[0].set_title("Ground Truth")
    axes[0].axis("off")

    axes[1].imshow(rendered_img)
    axes[1].set_title("Rendered NeRF")
    axes[1].axis("off")

    plt.tight_layout()
    plt.show()
    plt.close()


def generate_360_video(state, radius=DEFAULT_RADIUS, phi=60.0, num_frames=300):
    video_path = os.path.join(state.VISUALS_DIR, "nerf_360.mp4")
    writer = cv2.VideoWriter(
        video_path, cv2.VideoWriter_fourcc(*"mp4v"), 15, (100, 100)
    )

    for i in range(num_frames):
        theta = 360.0 * i / num_frames

        c2w = generate_pose_from_theta_phi(radius=radius, theta=theta, phi=phi)

        rays_o, rays_d = get_rays(100, 100, state.focal, c2w.to(device))
        rays_o = rays_o.reshape(-1, 3)
        rays_d = rays_d.reshape(-1, 3)

        outputs = nerf_forward(
            rays_o,
            rays_d,
            near,
            far,
            state.encode,
            state.model,
            curr_kwargs_sample_stratified=kwargs_sample_stratified,
            curr_n_samples_hierarchical=n_samples_hierarchical,
            curr_kwargs_sample_hierarchical=kwargs_sample_hierarchical,
            curr_fine_model=state.fine_model,
            viewdirs_encoding_fn=state.encode_viewdirs,
            curr_chunksize=chunksize,
        )
        rgb = outputs["rgb_map"].detach().reshape(100, 100, 3)
        rgb = torch.nan_to_num(rgb, nan=0.0, posinf=1.0, neginf=0.0).clamp(0, 1)

        rgb_uint8 = (rgb * 255).round().to(torch.uint8).cpu().numpy()
        bgr = cv2.cvtColor(rgb_uint8, cv2.COLOR_RGB2BGR)

        writer.write(bgr)
        click.secho(f"Frame written: {i}", fg="green")

    writer.release()

    click.secho(f"360 render saved to {video_path}", fg="green")


def generate_random_pose(
    radius=DEFAULT_RADIUS, theta_range=(0, 2 * math.pi), phi_range=(math.pi / 6, math.pi / 3)
):
    """
    Generates a random camera-to-world (c2w) matrix looking at the origin.

    - radius: distance from the origin
    - theta: azimuth angle (in the xy-plane)
    - phi: elevation angle (from vertical axis)

    Returns: torch.FloatTensor of shape (4, 4)
    """
    theta = torch.rand(1).item() * (theta_range[1] - theta_range[0]) + theta_range[0]
    phi = torch.rand(1).item() * (phi_range[1] - phi_range[0]) + phi_range[0]

    c2w = generate_pose_from_theta_phi(radius=radius, theta=math.degrees(theta), phi=math.degrees(phi))

    return c2w.to(device)


def generate_novel_view(state, c2w: torch.tensor):
    height, width = 100, 100
    rays_o, rays_d = get_rays(height, width, state.focal, c2w)
    rays_o = rays_o.reshape([-1, 3])
    rays_d = rays_d.reshape([-1, 3])

    # Run NeRF forward pass
    outputs = nerf_forward(
        rays_o,
        rays_d,
        near,
        far,
        state.encode,
        state.model,
        curr_kwargs_sample_stratified=kwargs_sample_stratified,
        curr_n_samples_hierarchical=n_samples_hierarchical,
        curr_kwargs_sample_hierarchical=kwargs_sample_hierarchical,
        curr_fine_model=state.fine_model,
        viewdirs_encoding_fn=state.encode_viewdirs,
        curr_chunksize=chunksize,
    )

    rgb = outputs["rgb_map"]
    image = rgb.reshape(height, width, 3).detach().cpu().numpy()

    fig = plt.figure(figsize=(14, 6))
    gs = GridSpec(1, 2, width_ratios=[1, 1.2])

    # === Left: Rendered Image ===
    ax_img = fig.add_subplot(gs[0])
    ax_img.imshow(image)
    ax_img.set_title("Rendered View")
    ax_img.axis("off")

    c2w = c2w.cpu().numpy()

    cam_pos = c2w[:3, 3]
    right = c2w[:3, 0]
    up = c2w[:3, 1]
    forward = -c2w[:3, 2]

    ax: Axes3D = fig.add_subplot(gs[1], projection="3d")

    ax.quiver(*cam_pos, *right, length=1.5, color="m", label="Novel Right")
    ax.quiver(*cam_pos, *up, length=1.5, color="c", label="Novel Up")
    ax.quiver(*cam_pos, *forward, length=1.5, color="y", label="Novel forward")

    ax.set_title("Camera Matrix")
    ax.set_xlabel("X")
    ax.set_ylabel("Y")
    ax.set_zlabel("Z")
    ax.view_init(elev=0, azim=-90)
    ax.legend()

    lim = 5

    ax.set_xlim([-lim, lim])
    ax.set_ylim([-lim, lim])
    ax.set_zlim([-lim, lim])

    plt.show()
    plt.close()


def generate_pose_from_theta_phi(radius=DEFAULT_RADIUS, theta=0, phi=0):
    click.secho(f"Theta degrees: {theta}", fg="yellow")
    click.secho(f"Phi degrees: {phi}", fg="yellow")

    theta = math.radians(theta)
    phi = math.radians(phi)

    cam_pos = torch.tensor(
        [
            radius * math.sin(phi) * math.cos(theta),  # X
            radius * math.sin(phi) * math.sin(theta),  # Y
            radius * math.cos(phi),  # Z (elevation)
        ]
    )

    forward = (torch.zeros(3) - cam_pos).detach()
    forward /= torch.norm(forward)
    up = torch.tensor([0, 0, 1], dtype=torch.float32)

    right = torch.cross(up, forward, dim=0)
    right = right / torch.norm(right)
    up = torch.cross(forward, right, dim=0)

    R = torch.stack([right, up, -forward], dim=1)
    c2w = torch.eye(4)
    c2w[:3, :3] = R
    c2w[:3, 3] = cam_pos

    return c2w.to(device)


def show_batch_random_poses(state, radius=DEFAULT_RADIUS, height=50, width=50):
    _, axes = plt.subplots(4, 6, figsize=(12, 8))
    c2w = None
    for i, ax in enumerate(axes.ravel()):
        click.secho(f"FRAME {i} STATS:", fg="blue")
        c2w = generate_random_pose(radius=radius)

        rays_o, rays_d = get_rays(height * 2, width * 2, state.focal, c2w)

        rays_o = rays_o.reshape([-1, 3])
        rays_d = rays_d.reshape([-1, 3])

        outputs = nerf_forward(
            rays_o,
            rays_d,
            near,
            far,
            state.encode,
            state.model,
            curr_kwargs_sample_stratified=kwargs_sample_stratified,
            curr_n_samples_hierarchical=n_samples_hierarchical,
            curr_kwargs_sample_hierarchical=kwargs_sample_hierarchical,
            curr_fine_model=state.fine_model,
            viewdirs_encoding_fn=state.encode_viewdirs,
            curr_chunksize=chunksize,
        )
        rgb: torch.Tensor = outputs["rgb_map"]

        image = rgb.reshape(height * 2, width * 2, 3)
        image = image.permute(2, 0, 1)
        image = to_PIL(image)
        image.resize((width, height), PILImage.LANCZOS)
        ax.imshow(image)
        ax.axis("off")

    plt.tight_layout()
    plt.show()


def ask_for_radius():
    radius = questionary.text(
        f"Enter radius (0-{far}):",
        validate=lambda val: (0 <= float(val) <= far if val != '' else False),
        default=f"{DEFAULT_RADIUS}",
    ).ask()
    if radius is None:
        raise KeyboardInterrupt

    return float(radius)


def ask_for_theta_phi():
    theta = float(
        questionary.text(
            "Enter θ (0–360):", validate=lambda val: (0 <= float(val) <= 360 if val != '' else False),
        ).ask()
    )
    phi = float(
        questionary.text(
            "Enter ϕ (0–90):", validate=lambda val: (0 <= float(val) <= 90 if val != '' else False),
        ).ask()
    )
    if theta is None or phi is None:
        raise KeyboardInterrupt

    return float(theta), float(phi)


def main():
    state = NeRFState()
    args = parse_args()
    asset_name = args.asset_name

    state.VISUALS_DIR = f"assets/{asset_name}/data/visuals"
    state.CHECKPOINTS_DIR = f"assets/{asset_name}/data/checkpoints"
    state.DATA_PATH = f"assets/{asset_name}/data/{asset_name}.npz"

    init_models(state)
    data = np.load(state.DATA_PATH)

    state.images = torch.from_numpy(data["images"]).to(device)
    state.poses = torch.from_numpy(data["poses"]).to(device)
    state.focal = torch.from_numpy(data["focal"]).to(dtype=torch.float32).to(device)

    while True:
        choice = questionary.select(
            "Choose an evaluation option:",
            choices=[
                "1. Replicate input pose index",
                "2. Generate 360-degree render",
                "3. Generate random pose",
                "4. Pose at custom θ/ϕ",
                "5. Show 24 random poses",
                "Exit",
            ],
        ).ask()

        if choice is None:
            break

        try:
            if choice.startswith("1"):
                while True:
                    try:
                        idx = int(input(f"Select test index (0 to {len(data['images']) - 1}): "))
                        break
                    except ValueError:
                        continue
                replicate_view(state, idx)

            elif choice.startswith("2"):
                radius_choice = ask_for_radius()
                generate_360_video(state, radius=radius_choice)

            elif choice.startswith("3"):
                radius_choice = ask_for_radius()
                c2w = generate_random_pose(radius=radius_choice)
                generate_novel_view(state, c2w)

            elif choice.startswith("4"):
                radius_choice = ask_for_radius()
                theta_choice, phi_choice = ask_for_theta_phi()

                c2w = generate_pose_from_theta_phi(radius=radius_choice, theta=theta_choice, phi=phi_choice)
                generate_novel_view(state, c2w)

            elif choice.startswith("5"):
                radius_choice = ask_for_radius()
                show_batch_random_poses(state, radius=radius_choice)

            else:
                break

        except KeyboardInterrupt:
            break

        click.secho("Done evaluating. Exiting...", fg="green")


if __name__ == "__main__":
    main()
